from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import math
import struct


MAGIC = b"EDUDB1\x00\x00"
VERSION = 1
HEADER_STRUCT = struct.Struct("<8sIQ")
RECORD_STRUCT = struct.Struct("<IBB7HIHHBBHH8H3H16HI4I")

REQUESTED_SIZES = [10, 100, 1000, 10_000, 1_000_000, 1_000_000_000]
DEFAULT_MAX_RECORDS = 1_000_000


@dataclass(frozen=True)
class ExpectedDigest:
    digest: int
    emitted_snapshots: int
    total_enrollment: int
    total_risk: int
    max_risk_tier: int
    last_school_id: int


def mix32(value: int) -> int:
    value &= 0xFFFFFFFF
    value ^= (value >> 16)
    value = (value * 0x7FEB352D) & 0xFFFFFFFF
    value ^= (value >> 15)
    value = (value * 0x846CA68B) & 0xFFFFFFFF
    value ^= (value >> 16)
    return value & 0xFFFFFFFF


def _weighted_average(values: list[int]) -> int:
    return (
        values[0]
        + 2 * values[1]
        + 3 * values[2]
        + 4 * values[3]
    ) // 10


def _school_count_for(size: int) -> int:
    return max(8, min(4096, int(math.sqrt(max(1, size))) * 4))


def _build_record(index: int, school_count: int) -> tuple[int, ...]:
    school_slot = index % school_count
    school_kind = school_slot % 4
    school_id = 1000 + school_slot
    region_code = (school_slot % 7) + 1
    district_code = 100 + (school_slot % 91)
    campus_code = 10 + (school_slot % 200)
    teacher_count = 25 + (school_slot % 90)
    subtype_a = 5 + (school_slot % 31)
    subtype_b = 7 + ((school_slot * 3) % 29)
    subtype_c = 11 + ((school_slot * 5) % 23)
    subtype_d = 13 + ((school_slot * 7) % 19)
    student_id = index + 1
    cohort = 2018 + (index % 8)
    postcode = 2000 + ((index * 17) % 7000)
    grade_level = 1 + (index % 12)
    program_track = index % 6
    support_bits = ((index * 5) ^ school_slot) & 0xFF
    guardian_count = 1 + (index % 4)
    tags = [((index + offset * 11 + school_slot * 3) % 32) for offset in range(8)]
    attendance_present = 140 + ((index * 7 + school_slot) % 60)
    attendance_absent = 1 + ((index * 3 + school_slot) % 20)
    attendance_late = (index + school_slot * 2) % 15
    maths = [45 + ((index + offset * 13 + school_slot) % 55) for offset in range(4)]
    reading = [40 + ((index + offset * 9 + school_slot * 2) % 60) for offset in range(4)]
    science = [42 + ((index + offset * 7 + school_slot * 5) % 58) for offset in range(4)]
    writing = [38 + ((index + offset * 5 + school_slot * 7) % 62) for offset in range(4)]
    tuition_band = 1000 + ((school_slot * 37 + index) % 5000)
    scholarship_amount = 500 + ((index * 17 + school_slot * 19) % 8000)
    outstanding_balance = 250 + ((index * 23 + school_slot * 31) % 10_000)
    household_income = 25_000 + ((index * 97 + school_slot * 101) % 150_000)
    compliance_flags = mix32(index * 97 + school_slot * 389) & 0xFFFF
    return (
        school_id,
        school_kind,
        region_code,
        district_code,
        campus_code,
        teacher_count,
        subtype_a,
        subtype_b,
        subtype_c,
        subtype_d,
        student_id,
        cohort,
        postcode,
        grade_level,
        program_track,
        support_bits,
        guardian_count,
        *tags,
        attendance_present,
        attendance_absent,
        attendance_late,
        *maths,
        *reading,
        *science,
        *writing,
        tuition_band,
        scholarship_amount,
        outstanding_balance,
        household_income,
        compliance_flags,
    )


def generate_dataset(path: Path, size: int) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    school_count = _school_count_for(size)
    with path.open("wb") as handle:
        handle.write(HEADER_STRUCT.pack(MAGIC, VERSION, size))
        for index in range(size):
            handle.write(RECORD_STRUCT.pack(*_build_record(index, school_count)))


def iter_records(path: Path):
    with path.open("rb") as handle:
        magic, version, count = HEADER_STRUCT.unpack(handle.read(HEADER_STRUCT.size))
        if magic != MAGIC or version != VERSION:
            raise ValueError(f"Unsupported dataset header for {path}")
        for _ in range(count):
            raw = handle.read(RECORD_STRUCT.size)
            if len(raw) != RECORD_STRUCT.size:
                raise ValueError(f"Unexpected EOF in {path}")
            yield RECORD_STRUCT.unpack(raw)


def _school_special_index(
    school_kind: int,
    teacher_count: int,
    subtype_a: int,
    subtype_b: int,
    subtype_c: int,
    subtype_d: int,
) -> int:
    if school_kind == 0:
        return teacher_count + subtype_a * 4 + subtype_b * 3 + subtype_c * 2 + subtype_d
    if school_kind == 1:
        return teacher_count + subtype_a * 2 + subtype_b * 5 + subtype_c + subtype_d
    if school_kind == 2:
        return teacher_count + subtype_a * 3 + subtype_b * 2 + subtype_c * 4 + subtype_d
    return teacher_count + subtype_a + subtype_b * 2 + subtype_c * 3 + subtype_d * 4


def expected_digest_for(path: Path) -> ExpectedDigest:
    records = list(iter_records(path))
    schools: dict[int, dict[str, object]] = {}
    digest = {
        "digest": 0,
        "emitted_snapshots": 0,
        "total_enrollment": 0,
        "total_risk": 0,
        "max_risk_tier": 0,
        "last_school_id": 0,
    }
    last_signature = None
    started = False

    for index in range(max(0, len(records) - 3)):
        record = records[index]
        secondary = records[index + 1]
        tertiary = records[index + 2]
        quaternary = records[index + 3]
        school_id = record[0]
        school_kind = record[1]
        teacher_count = record[5]
        subtype_a, subtype_b, subtype_c, subtype_d = record[6:10]
        student_id = record[10] ^ secondary[10] ^ tertiary[10] ^ quaternary[10]
        cohort = record[11]
        grade_level = record[13]
        program_track = record[14] ^ secondary[14] ^ tertiary[14] ^ quaternary[14]
        support_bits = record[15]
        tags = [
            record[17], record[18], record[19], record[20],
            secondary[17], secondary[18],
            tertiary[17], tertiary[18],
            quaternary[17], quaternary[18],
        ]
        attendance_present = record[25] + secondary[25] + tertiary[25] + quaternary[25]
        attendance_absent = record[26] + secondary[26] + tertiary[26] + quaternary[26]
        attendance_late = record[27] + secondary[27] + tertiary[27] + quaternary[27]
        maths = [record[28], record[29], record[30], record[31]]
        reading = [record[32], record[33], record[34], record[35]]
        science = [record[36], record[37], record[38], record[39]]
        writing = [record[40], record[41], record[42], record[43]]
        scholarship_amount = record[45] + secondary[45] + tertiary[45] + quaternary[45]
        outstanding_balance = record[46] + secondary[46] + tertiary[46] + quaternary[46]

        maths_avg = _weighted_average(maths) + _weighted_average(list(secondary[28:32]))
        reading_avg = _weighted_average(reading) + _weighted_average(list(tertiary[32:36]))
        science_avg = _weighted_average(science) + _weighted_average(list(quaternary[36:40]))
        writing_avg = _weighted_average(writing) + _weighted_average(list(secondary[40:44]))
        composite = (
            maths_avg * 35
            + reading_avg * 25
            + science_avg * 20
            + writing_avg * 20
        ) // 100
        attendance_rate = (attendance_present * 10_000) // max(
            1, attendance_present + attendance_absent + attendance_late
        )
        aid_score = int(record[45]) - int(record[46] // 2)
        support_score = (
            (1 if (record[15] & 0x01) else 0)
            + (1 if (secondary[15] & 0x02) else 0)
            + (1 if (tertiary[15] & 0x04) else 0)
            + (1 if (quaternary[15] & 0x08) else 0)
        )
        special_index = _school_special_index(
            school_kind,
            teacher_count,
            subtype_a,
            subtype_b,
            subtype_c,
            subtype_d,
        )
        profile_hash = mix32(
            student_id
            ^ (composite << 1)
            ^ (attendance_rate << 2)
            ^ ((aid_score + int(secondary[45]) - int(secondary[46] // 2)) << 3)
            ^ (grade_level << 7)
            ^ (program_track << 11)
            ^ ((record[11] ^ secondary[11] ^ tertiary[11] ^ quaternary[11]) << 13)
        )
        tag_vector = tags + [
            (record[2] + secondary[2] + tertiary[2] + quaternary[2]) & 31,
            (record[15] ^ secondary[14] ^ tertiary[11] ^ quaternary[13]) & 31,
        ]

        school = schools.get(school_id)
        if school is None:
            school = {
                "active_count": 0,
                "scholarship_count": 0,
                "support_count": 0,
                "composite_sum": 0,
                "attendance_sum": 0,
                "funding_sum": 0,
                "compliance_mix": 0,
                "tag_hist": [0] * 32,
            }
            schools[school_id] = school

        school["active_count"] = int(school["active_count"]) + 1
        school["scholarship_count"] = int(school["scholarship_count"]) + (1 if scholarship_amount > 0 else 0)
        school["support_count"] = int(school["support_count"]) + (1 if support_score >= 2 else 0)
        school["composite_sum"] = int(school["composite_sum"]) + composite
        school["attendance_sum"] = int(school["attendance_sum"]) + attendance_rate
        school["funding_sum"] = int(school["funding_sum"]) + int(outstanding_balance) - int(scholarship_amount // 2) + special_index
        school["compliance_mix"] = mix32(int(school["compliance_mix"]) ^ profile_hash ^ special_index)
        tag_hist = school["tag_hist"]
        assert isinstance(tag_hist, list)
        for tag in tag_vector:
            tag_hist[tag & 31] += 1

        active_count = int(school["active_count"])
        avg_composite = int(school["composite_sum"]) // active_count
        avg_attendance = int(school["attendance_sum"]) // active_count
        funding_pressure = int(school["funding_sum"]) // active_count
        absent_pressure = attendance_absent * 37 + attendance_late * 19
        risk_score = max(
            0,
            15_000
            - avg_composite * 2
            + funding_pressure // 4
            + int(school["support_count"]) * 13
            + absent_pressure
            + special_index * 5
            + (int(school["compliance_mix"]) & 127),
        )
        risk_tier = 2 if risk_score >= 9000 else 1 if risk_score >= 7000 else 0
        top_buckets = sorted(range(32), key=lambda bucket: (tag_hist[bucket], -bucket), reverse=True)[:6]
        signature = mix32(
            school_id
            ^ (risk_tier << 6)
            ^ (active_count << 9)
            ^ (avg_composite << 1)
            ^ (avg_attendance << 3)
            ^ (funding_pressure << 5)
            ^ top_buckets[0]
            ^ (top_buckets[1] << 8)
        )

        if active_count < 2 or (risk_tier == 0 and funding_pressure <= 100):
            continue
        if signature == last_signature:
            continue
        last_signature = signature
        if not started:
            if active_count < 3:
                continue
            started = True

        digest["emitted_snapshots"] += 1
        digest["total_enrollment"] += active_count
        digest["total_risk"] += risk_score
        digest["max_risk_tier"] = max(digest["max_risk_tier"], risk_tier)
        digest["last_school_id"] = school_id
        digest["digest"] = mix32(
            digest["digest"]
            ^ mix32(school_id)
            ^ mix32(active_count)
            ^ mix32(risk_score)
            ^ mix32(funding_pressure)
            ^ mix32(risk_tier)
        )

    return ExpectedDigest(**digest)
