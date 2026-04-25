const fs = require("fs");
const { from: rxFrom, zip: rxZip } = require("rxjs");
const {
  map,
  scan,
  filter,
  distinctUntilChanged,
  skipWhile,
  tap,
  reduce,
} = require("rxjs/operators");

const MAGIC = "EDUDB1\0\0";
const HEADER_SIZE = 20;
const TAG_COUNT = 8;
const EXTENDED_TAG_COUNT = 12;
const HIST_BUCKETS = 32;
const TOP_BUCKETS = 6;

function mix32(value) {
  value >>>= 0;
  value ^= value >>> 16;
  value = Math.imul(value, 0x7feb352d) >>> 0;
  value ^= value >>> 15;
  value = Math.imul(value, 0x846ca68b) >>> 0;
  value ^= value >>> 16;
  return value >>> 0;
}

function weightedAverage(values) {
  return Math.floor((values[0] + 2 * values[1] + 3 * values[2] + 4 * values[3]) / 10);
}

function schoolSpecialIndex(school) {
  switch (school.kind) {
    case 0:
      return school.teacherCount + school.subtypeA * 4 + school.subtypeB * 3 + school.subtypeC * 2 + school.subtypeD;
    case 1:
      return school.teacherCount + school.subtypeA * 2 + school.subtypeB * 5 + school.subtypeC + school.subtypeD;
    case 2:
      return school.teacherCount + school.subtypeA * 3 + school.subtypeB * 2 + school.subtypeC * 4 + school.subtypeD;
    default:
      return school.teacherCount + school.subtypeA + school.subtypeB * 2 + school.subtypeC * 3 + school.subtypeD * 4;
  }
}

function chooseTopBuckets(hist) {
  const buckets = Array.from({ length: HIST_BUCKETS }, (_, index) => index);
  buckets.sort((left, right) => {
    if (hist[right] !== hist[left]) {
      return hist[right] - hist[left];
    }
    return left - right;
  });
  return buckets.slice(0, TOP_BUCKETS);
}

function loadRecords(path) {
  const raw = fs.readFileSync(path);
  const magic = raw.subarray(0, 8).toString("latin1");
  const version = raw.readUInt32LE(8);
  const count = Number(raw.readBigUInt64LE(12));
  if (magic !== MAGIC || version !== 1) {
    throw new Error("unsupported dataset format");
  }

  const schools = new Map();
  const records = [];
  let offset = HEADER_SIZE;
  for (let index = 0; index < count; index += 1) {
    const schoolId = raw.readUInt32LE(offset); offset += 4;
    const schoolKind = raw.readUInt8(offset); offset += 1;
    const regionCode = raw.readUInt8(offset); offset += 1;
    const districtCode = raw.readUInt16LE(offset); offset += 2;
    const campusCode = raw.readUInt16LE(offset); offset += 2;
    const teacherCount = raw.readUInt16LE(offset); offset += 2;
    const subtypeA = raw.readUInt16LE(offset); offset += 2;
    const subtypeB = raw.readUInt16LE(offset); offset += 2;
    const subtypeC = raw.readUInt16LE(offset); offset += 2;
    const subtypeD = raw.readUInt16LE(offset); offset += 2;
    const studentId = raw.readUInt32LE(offset); offset += 4;
    const cohort = raw.readUInt16LE(offset); offset += 2;
    const postcode = raw.readUInt16LE(offset); offset += 2;
    const gradeLevel = raw.readUInt8(offset); offset += 1;
    const programTrack = raw.readUInt8(offset); offset += 1;
    const supportBits = raw.readUInt16LE(offset); offset += 2;
    const guardianCount = raw.readUInt16LE(offset); offset += 2;
    const tags = Array.from({ length: TAG_COUNT }, () => {
      const value = raw.readUInt16LE(offset);
      offset += 2;
      return value;
    });
    const attendancePresent = raw.readUInt16LE(offset); offset += 2;
    const attendanceAbsent = raw.readUInt16LE(offset); offset += 2;
    const attendanceLate = raw.readUInt16LE(offset); offset += 2;
    const maths = Array.from({ length: 4 }, () => {
      const value = raw.readUInt16LE(offset);
      offset += 2;
      return value;
    });
    const reading = Array.from({ length: 4 }, () => {
      const value = raw.readUInt16LE(offset);
      offset += 2;
      return value;
    });
    const science = Array.from({ length: 4 }, () => {
      const value = raw.readUInt16LE(offset);
      offset += 2;
      return value;
    });
    const writing = Array.from({ length: 4 }, () => {
      const value = raw.readUInt16LE(offset);
      offset += 2;
      return value;
    });
    const tuitionBand = raw.readUInt32LE(offset); offset += 4;
    const scholarshipAmount = raw.readUInt32LE(offset); offset += 4;
    const outstandingBalance = raw.readUInt32LE(offset); offset += 4;
    const householdIncome = raw.readUInt32LE(offset); offset += 4;
    const complianceFlags = raw.readUInt32LE(offset); offset += 4;

    let school = schools.get(schoolId);
    if (!school) {
      school = {
        kind: schoolKind,
        schoolId,
        regionCode,
        districtCode,
        campusCode,
        teacherCount,
        subtypeA,
        subtypeB,
        subtypeC,
        subtypeD,
      };
      schools.set(schoolId, school);
    }

    records.push({
      school,
      studentId,
      cohort,
      postcode,
      gradeLevel,
      programTrack,
      supportBits,
      guardianCount,
      tags,
      attendancePresent,
      attendanceAbsent,
      attendanceLate,
      maths,
      reading,
      science,
      writing,
      tuitionBand,
      scholarshipAmount,
      outstandingBalance,
      householdIncome,
      complianceFlags,
    });
  }

  return records;
}

function enrichRecord(record) {
  const attendancePresent =
    record.primary.attendancePresent +
    record.secondary.attendancePresent +
    record.tertiary.attendancePresent +
    record.quaternary.attendancePresent;
  const attendanceAbsent =
    record.primary.attendanceAbsent +
    record.secondary.attendanceAbsent +
    record.tertiary.attendanceAbsent +
    record.quaternary.attendanceAbsent;
  const attendanceLate =
    record.primary.attendanceLate +
    record.secondary.attendanceLate +
    record.tertiary.attendanceLate +
    record.quaternary.attendanceLate;
  const attendanceRate = Math.floor(
    (attendancePresent * 10000) /
      Math.max(1, attendancePresent + attendanceAbsent + attendanceLate)
  );
  const averages = [
    weightedAverage(record.primary.maths) + weightedAverage(record.secondary.maths),
    weightedAverage(record.primary.reading) + weightedAverage(record.tertiary.reading),
    weightedAverage(record.primary.science) + weightedAverage(record.quaternary.science),
    weightedAverage(record.primary.writing) + weightedAverage(record.secondary.writing),
  ];
  const composite = Math.floor(
    (averages[0] * 35 + averages[1] * 25 + averages[2] * 20 + averages[3] * 20) / 100
  );
  const scholarshipAmount =
    record.primary.scholarshipAmount +
    record.secondary.scholarshipAmount +
    record.tertiary.scholarshipAmount +
    record.quaternary.scholarshipAmount;
  const outstandingBalance =
    record.primary.outstandingBalance +
    record.secondary.outstandingBalance +
    record.tertiary.outstandingBalance +
    record.quaternary.outstandingBalance;
  const aidScore = record.primary.scholarshipAmount - Math.floor(record.primary.outstandingBalance / 2);
  const supportScore =
    ((record.primary.supportBits & 0x01) ? 1 : 0) +
    ((record.secondary.supportBits & 0x02) ? 1 : 0) +
    ((record.tertiary.supportBits & 0x04) ? 1 : 0) +
    ((record.quaternary.supportBits & 0x08) ? 1 : 0);
  const tagVector = [
    record.primary.tags[0],
    record.primary.tags[1],
    record.primary.tags[2],
    record.primary.tags[3],
    record.secondary.tags[0],
    record.secondary.tags[1],
    record.tertiary.tags[0],
    record.tertiary.tags[1],
    record.quaternary.tags[0],
    record.quaternary.tags[1],
    (record.primary.school.regionCode + record.secondary.school.regionCode + record.tertiary.school.regionCode + record.quaternary.school.regionCode) & 31,
    (record.primary.supportBits ^ record.secondary.programTrack ^ record.tertiary.cohort ^ record.quaternary.gradeLevel) & 31,
  ];
  const profileHash = mix32(
    (record.primary.studentId ^ record.secondary.studentId ^ record.tertiary.studentId ^ record.quaternary.studentId) ^
      (composite << 1) ^
      (attendanceRate << 2) ^
      ((aidScore + record.secondary.scholarshipAmount - Math.floor(record.secondary.outstandingBalance / 2)) << 3) ^
      (record.primary.gradeLevel << 7) ^
      ((record.primary.programTrack ^ record.secondary.programTrack ^ record.tertiary.programTrack ^ record.quaternary.programTrack) << 11) ^
      ((record.primary.cohort ^ record.secondary.cohort ^ record.tertiary.cohort ^ record.quaternary.cohort) << 13)
  );
  return {
    school: record.primary.school,
    studentId: record.primary.studentId,
    cohort: record.primary.cohort,
    postcode: record.primary.postcode,
    gradeLevel: record.primary.gradeLevel,
    programTrack: record.primary.programTrack,
    guardianCount:
      record.primary.guardianCount +
      record.secondary.guardianCount +
      record.tertiary.guardianCount +
      record.quaternary.guardianCount,
    attendanceRate,
    attendanceAbsent,
    attendanceLate,
    scholarshipAmount,
    outstandingBalance,
    aidScore,
    supportScore,
    averages,
    composite,
    tagVector,
    profileHash,
    schoolKey: `school:${record.primary.school.kind}:${record.primary.school.regionCode}:${record.primary.school.districtCode}:${record.primary.school.schoolId}:${record.secondary.school.schoolId}:${record.tertiary.school.schoolId}`,
  };
}

function createDatabaseState() {
  return {
    schools: new Map(),
    lastTouched: null,
    lastEnriched: null,
  };
}

function updateDatabaseState(state, enriched) {
  const nextState = state ?? createDatabaseState();
  let aggregate = nextState.schools.get(enriched.school.schoolId);
  if (!aggregate) {
    aggregate = {
      school: enriched.school,
      activeCount: 0,
      scholarshipCount: 0,
      supportCount: 0,
      compositeSum: 0,
      attendanceSum: 0,
      fundingSum: 0,
      complianceMix: 0,
      tagHist: Array.from({ length: HIST_BUCKETS }, () => 0),
    };
    nextState.schools.set(enriched.school.schoolId, aggregate);
  }

  const specialIndex = schoolSpecialIndex(enriched.school);
  aggregate.activeCount += 1;
  aggregate.scholarshipCount += enriched.scholarshipAmount > 0 ? 1 : 0;
  aggregate.supportCount += enriched.supportScore >= 2 ? 1 : 0;
  aggregate.compositeSum += enriched.composite;
  aggregate.attendanceSum += enriched.attendanceRate;
  aggregate.fundingSum += enriched.outstandingBalance - Math.floor(enriched.scholarshipAmount / 2) + specialIndex;
  aggregate.complianceMix = mix32(aggregate.complianceMix ^ enriched.profileHash ^ specialIndex);
  for (const tag of enriched.tagVector) {
    aggregate.tagHist[tag & 31] += 1;
  }

  nextState.lastTouched = aggregate;
  nextState.lastEnriched = enriched;
  return nextState;
}

function buildSchoolSnapshot(state) {
  const aggregate = state.lastTouched;
  const enriched = state.lastEnriched;
  const activeCount = aggregate.activeCount;
  const avgComposite = Math.floor(aggregate.compositeSum / activeCount);
  const avgAttendance = Math.floor(aggregate.attendanceSum / activeCount);
  const fundingPressure = Math.floor(aggregate.fundingSum / activeCount);
  const specialIndex = schoolSpecialIndex(aggregate.school);
  const absentPressure = enriched.attendanceAbsent * 37 + enriched.attendanceLate * 19;
  let riskScore =
    15000 -
    avgComposite * 2 +
    Math.floor(fundingPressure / 4) +
    aggregate.supportCount * 13 +
    absentPressure +
    specialIndex * 5 +
    (aggregate.complianceMix & 127);
  if (riskScore < 0) {
    riskScore = 0;
  }
  const riskTier = riskScore >= 9000 ? 2 : riskScore >= 7000 ? 1 : 0;
  const topBuckets = chooseTopBuckets(aggregate.tagHist);
  const signature = mix32(
    aggregate.school.schoolId ^
      (riskTier << 6) ^
      (activeCount << 9) ^
      (avgComposite << 1) ^
      (avgAttendance << 3) ^
      (fundingPressure << 5) ^
      topBuckets[0] ^
      (topBuckets[1] << 8)
  );
  return {
    school: aggregate.school,
    activeCount,
    scholarshipCount: aggregate.scholarshipCount,
    supportCount: aggregate.supportCount,
    avgComposite,
    avgAttendance,
    fundingPressure,
    riskScore,
    riskTier,
    signature,
    topBuckets,
    exportRow: `${aggregate.school.kind}|${aggregate.school.regionCode}|${aggregate.school.schoolId}|${riskTier}|${activeCount}|${avgComposite}`,
    alertFlags: Array.from({ length: 8 }, (_, index) => (riskScore >> (index * 2)) & 3),
  };
}

function snapshotIsRelevant(snapshot) {
  return snapshot.activeCount >= 2 && (snapshot.riskTier > 0 || snapshot.fundingPressure > 100);
}

function snapshotSignature(snapshot) {
  return snapshot.signature;
}

function snapshotIsCold(snapshot) {
  return snapshot.activeCount < 3;
}

function accumulateDigest(accum, snapshot) {
  const digest = accum ?? {
    digest: 0,
    emittedSnapshots: 0,
    totalEnrollment: 0,
    totalRisk: 0,
    maxRiskTier: 0,
    lastSchoolId: 0,
  };
  digest.emittedSnapshots += 1;
  digest.totalEnrollment += snapshot.activeCount;
  digest.totalRisk += snapshot.riskScore;
  digest.maxRiskTier = Math.max(digest.maxRiskTier, snapshot.riskTier);
  digest.lastSchoolId = snapshot.school.schoolId;
  digest.digest = mix32(
    digest.digest ^
      mix32(snapshot.school.schoolId) ^
      mix32(snapshot.activeCount) ^
      mix32(snapshot.riskScore) ^
      mix32(snapshot.fundingPressure) ^
      mix32(snapshot.riskTier)
  );
  return digest;
}

function renderProgress(processed, total) {
  const safeTotal = Math.max(1, total);
  const percent = Math.min(100, (processed * 100) / safeTotal);
  const width = 30;
  const filled = Math.min(width, Math.floor((percent / 100) * width));
  const bar = `${"#".repeat(filled)}${"-".repeat(width - filled)}`;
  process.stderr.write(`\r  TS progress [${bar}] ${percent.toFixed(2)}%`);
}

function runPipeline(records) {
  const totalItems = Math.max(0, records.length - 3);
  let processedItems = 0;
  let lastShown = -1;
  return new Promise((resolve, reject) => {
    rxZip(
      rxFrom(records),
      rxFrom(records.slice(1)),
      rxFrom(records.slice(2)),
      rxFrom(records.slice(3))
    )
      .pipe(
        map(([primary, secondary, tertiary, quaternary]) => ({ primary, secondary, tertiary, quaternary })),
        tap(() => {
          processedItems += 1;
          const currentShown = Math.floor((processedItems * 10000) / Math.max(1, totalItems));
          if (currentShown !== lastShown) {
            lastShown = currentShown;
            renderProgress(processedItems, totalItems);
          }
        }),
        map(enrichRecord),
        scan(updateDatabaseState, null),
        map(buildSchoolSnapshot),
        filter(snapshotIsRelevant),
        distinctUntilChanged((left, right) => snapshotSignature(left) === snapshotSignature(right)),
        skipWhile(snapshotIsCold),
        reduce(accumulateDigest, null)
      )
      .subscribe({
        next: (value) => {
          renderProgress(totalItems, totalItems);
          process.stderr.write("\n");
          resolve(value ?? {
            digest: 0,
            emittedSnapshots: 0,
            totalEnrollment: 0,
            totalRisk: 0,
            maxRiskTier: 0,
            lastSchoolId: 0,
          });
        },
        error: reject,
      });
  });
}

async function main() {
  const dataPath = process.argv[2];
  const runs = process.argv[3] ? Number(process.argv[3]) : 3;
  if (!dataPath) {
    throw new Error("usage: node edu_complex_benchmark.ts <data.bin> [runs]");
  }

  const records = loadRecords(dataPath);
  let totalNs = 0n;
  let result = null;
  for (let runIndex = 0; runIndex < runs; runIndex += 1) {
    const start = process.hrtime.bigint();
    result = await runPipeline(records);
    const end = process.hrtime.bigint();
    totalNs += end - start;
  }

  const finalResult = result ?? {
    digest: 0,
    emittedSnapshots: 0,
    totalEnrollment: 0,
    totalRisk: 0,
    maxRiskTier: 0,
    lastSchoolId: 0,
  };
  console.log(JSON.stringify({
    digest: finalResult.digest >>> 0,
    emitted_snapshots: finalResult.emittedSnapshots,
    total_enrollment: finalResult.totalEnrollment,
    total_risk: finalResult.totalRisk,
    max_risk_tier: finalResult.maxRiskTier,
    last_school_id: finalResult.lastSchoolId,
    average_ms: Number(totalNs) / runs / 1e6,
    runs,
  }));
}

main().catch((error) => {
  console.error(error.stack || error.message);
  process.exit(1);
});
