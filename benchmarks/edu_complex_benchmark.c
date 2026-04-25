#define _POSIX_C_SOURCE 200809L
#include "observable.h"

#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAGIC "EDUDB1\0\0"
#define VERSION 1U
#define TAG_COUNT 8
#define EXTENDED_TAG_COUNT 12
#define HIST_BUCKETS 32
#define TOP_BUCKETS 6

typedef enum
{
    SCHOOL_UNIVERSITY = 0,
    SCHOOL_PRIMARY = 1,
    SCHOOL_HIGH = 2,
    SCHOOL_DISTRICT = 3
} SchoolKind;

typedef struct SchoolBase SchoolBase;

typedef struct
{
    int (*special_index)(const SchoolBase *school);
} SchoolOps;

struct SchoolBase
{
    const SchoolOps *ops;
    SchoolKind kind;
    uint32_t school_id;
    uint8_t region_code;
    uint16_t district_code;
    uint16_t campus_code;
    uint16_t teacher_count;
};

typedef struct
{
    SchoolBase base;
    uint16_t subtype_a;
    uint16_t subtype_b;
    uint16_t subtype_c;
    uint16_t subtype_d;
} DerivedSchool;

#pragma pack(push, 1)
typedef struct
{
    char magic[8];
    uint32_t version;
    uint64_t count;
} FileHeader;

typedef struct
{
    uint32_t school_id;
    uint8_t school_kind;
    uint8_t region_code;
    uint16_t district_code;
    uint16_t campus_code;
    uint16_t teacher_count;
    uint16_t subtype_a;
    uint16_t subtype_b;
    uint16_t subtype_c;
    uint16_t subtype_d;
    uint32_t student_id;
    uint16_t cohort;
    uint16_t postcode;
    uint8_t grade_level;
    uint8_t program_track;
    uint16_t support_bits;
    uint16_t guardian_count;
    uint16_t tags[TAG_COUNT];
    uint16_t attendance_present;
    uint16_t attendance_absent;
    uint16_t attendance_late;
    uint16_t maths[4];
    uint16_t reading[4];
    uint16_t science[4];
    uint16_t writing[4];
    uint32_t tuition_band;
    uint32_t scholarship_amount;
    uint32_t outstanding_balance;
    uint32_t household_income;
    uint32_t compliance_flags;
} DiskRecord;
#pragma pack(pop)

typedef struct
{
    SchoolBase *school;
    uint32_t student_id;
    uint16_t cohort;
    uint16_t postcode;
    uint8_t grade_level;
    uint8_t program_track;
    uint16_t support_bits;
    uint16_t guardian_count;
    uint16_t tags[TAG_COUNT];
    uint16_t attendance_present;
    uint16_t attendance_absent;
    uint16_t attendance_late;
    uint16_t maths[4];
    uint16_t reading[4];
    uint16_t science[4];
    uint16_t writing[4];
    uint32_t tuition_band;
    uint32_t scholarship_amount;
    uint32_t outstanding_balance;
    uint32_t household_income;
    uint32_t compliance_flags;
} StudentRecord;

typedef struct
{
    StudentRecord *primary;
    StudentRecord *secondary;
    StudentRecord *tertiary;
    StudentRecord *quaternary;
} ZippedRecord;

typedef struct
{
    SchoolBase *school;
    uint32_t student_id;
    uint16_t cohort;
    uint16_t postcode;
    uint8_t grade_level;
    uint8_t program_track;
    uint16_t guardian_count;
    uint16_t attendance_rate;
    uint16_t attendance_absent;
    uint16_t attendance_late;
    uint32_t scholarship_amount;
    uint32_t outstanding_balance;
    int32_t aid_score;
    int32_t support_score;
    int32_t averages[4];
    int32_t composite;
    uint16_t tag_vector[EXTENDED_TAG_COUNT];
    uint32_t profile_hash;
    char school_key[64];
} EnrichedRecord;

typedef struct
{
    bool used;
    SchoolBase *school;
    uint32_t active_count;
    uint32_t scholarship_count;
    uint32_t support_count;
    uint64_t composite_sum;
    uint64_t attendance_sum;
    int64_t funding_sum;
    uint32_t compliance_mix;
    uint32_t tag_hist[HIST_BUCKETS];
} SchoolAggregate;

typedef struct
{
    SchoolAggregate *table;
    int capacity;
    SchoolAggregate *last_touched;
    EnrichedRecord *last_enriched;
} DatabaseState;

typedef struct
{
    SchoolBase *school;
    uint32_t active_count;
    uint32_t scholarship_count;
    uint32_t support_count;
    uint32_t avg_composite;
    uint32_t avg_attendance;
    int32_t funding_pressure;
    uint32_t risk_score;
    uint32_t risk_tier;
    uint32_t signature;
    uint32_t top_buckets[TOP_BUCKETS];
    char export_row[128];
    uint32_t alert_flags[8];
} SchoolSnapshot;

typedef struct
{
    uint32_t digest;
    uint64_t emitted_snapshots;
    uint64_t total_enrollment;
    uint64_t total_risk;
    uint32_t max_risk_tier;
    uint32_t last_school_id;
} AuditDigest;

static AuditDigest *g_result = NULL;

static uint32_t mix32(uint32_t value)
{
    value ^= value >> 16;
    value *= 0x7FEB352Du;
    value ^= value >> 15;
    value *= 0x846CA68Bu;
    value ^= value >> 16;
    return value;
}

static int weighted_average(const uint16_t values[4])
{
    return (int)(values[0] + 2 * values[1] + 3 * values[2] + 4 * values[3]) / 10;
}

static int synthetic_school_count_for(uint64_t size)
{
    double root = sqrt((double)(size > 0 ? size : 1));
    int scaled = (int)root * 4;
    if (scaled < 8)
    {
        scaled = 8;
    }
    if (scaled > 4096)
    {
        scaled = 4096;
    }
    return scaled;
}

static int64_t floor_div_i64(int64_t numerator, int64_t denominator)
{
    int64_t quotient = numerator / denominator;
    int64_t remainder = numerator % denominator;
    if (remainder != 0 && ((numerator < 0) != (denominator < 0)))
    {
        quotient -= 1;
    }
    return quotient;
}

static int school_special_index(const SchoolBase *school)
{
    const DerivedSchool *derived = (const DerivedSchool *)school;
    switch (school->kind)
    {
        case SCHOOL_UNIVERSITY:
            return school->teacher_count + derived->subtype_a * 4 + derived->subtype_b * 3 +
                derived->subtype_c * 2 + derived->subtype_d;
        case SCHOOL_PRIMARY:
            return school->teacher_count + derived->subtype_a * 2 + derived->subtype_b * 5 +
                derived->subtype_c + derived->subtype_d;
        case SCHOOL_HIGH:
            return school->teacher_count + derived->subtype_a * 3 + derived->subtype_b * 2 +
                derived->subtype_c * 4 + derived->subtype_d;
        default:
            return school->teacher_count + derived->subtype_a + derived->subtype_b * 2 +
                derived->subtype_c * 3 + derived->subtype_d * 4;
    }
}

static SchoolBase *make_school(const DiskRecord *record)
{
    DerivedSchool *school = malloc(sizeof(*school));
    memset(school, 0, sizeof(*school));
    school->base.ops = NULL;
    school->base.kind = (SchoolKind)record->school_kind;
    school->base.school_id = record->school_id;
    school->base.region_code = record->region_code;
    school->base.district_code = record->district_code;
    school->base.campus_code = record->campus_code;
    school->base.teacher_count = record->teacher_count;
    school->subtype_a = record->subtype_a;
    school->subtype_b = record->subtype_b;
    school->subtype_c = record->subtype_c;
    school->subtype_d = record->subtype_d;
    return &school->base;
}

static uint32_t school_hash(uint32_t school_id)
{
    return mix32(school_id * 2654435761u);
}

static SchoolBase **load_school_slot(SchoolBase **table, int capacity, uint32_t school_id)
{
    uint32_t slot = school_hash(school_id) & (uint32_t)(capacity - 1);
    for (;;)
    {
        SchoolBase **entry = &table[slot];
        if (*entry == NULL || (*entry)->school_id == school_id)
        {
            return entry;
        }
        slot = (slot + 1u) & (uint32_t)(capacity - 1);
    }
}

static void build_disk_record(uint64_t index, int school_count, DiskRecord *record)
{
    uint64_t school_slot = index % (uint64_t)school_count;
    memset(record, 0, sizeof(*record));
    record->school_id = 1000u + (uint32_t)school_slot;
    record->school_kind = (uint8_t)(school_slot % 4u);
    record->region_code = (uint8_t)((school_slot % 7u) + 1u);
    record->district_code = (uint16_t)(100u + (school_slot % 91u));
    record->campus_code = (uint16_t)(10u + (school_slot % 200u));
    record->teacher_count = (uint16_t)(25u + (school_slot % 90u));
    record->subtype_a = (uint16_t)(5u + (school_slot % 31u));
    record->subtype_b = (uint16_t)(7u + ((school_slot * 3u) % 29u));
    record->subtype_c = (uint16_t)(11u + ((school_slot * 5u) % 23u));
    record->subtype_d = (uint16_t)(13u + ((school_slot * 7u) % 19u));
    record->student_id = (uint32_t)(index + 1u);
    record->cohort = (uint16_t)(2018u + (index % 8u));
    record->postcode = (uint16_t)(2000u + ((index * 17u) % 7000u));
    record->grade_level = (uint8_t)(1u + (index % 12u));
    record->program_track = (uint8_t)(index % 6u);
    record->support_bits = (uint16_t)(((index * 5u) ^ school_slot) & 0xFFu);
    record->guardian_count = (uint16_t)(1u + (index % 4u));
    for (int offset = 0; offset < TAG_COUNT; ++offset)
    {
        record->tags[offset] = (uint16_t)((index + (uint64_t)offset * 11u + school_slot * 3u) % 32u);
    }
    record->attendance_present = (uint16_t)(140u + ((index * 7u + school_slot) % 60u));
    record->attendance_absent = (uint16_t)(1u + ((index * 3u + school_slot) % 20u));
    record->attendance_late = (uint16_t)((index + school_slot * 2u) % 15u);
    for (int offset = 0; offset < 4; ++offset)
    {
        record->maths[offset] = (uint16_t)(45u + ((index + (uint64_t)offset * 13u + school_slot) % 55u));
        record->reading[offset] = (uint16_t)(40u + ((index + (uint64_t)offset * 9u + school_slot * 2u) % 60u));
        record->science[offset] = (uint16_t)(42u + ((index + (uint64_t)offset * 7u + school_slot * 5u) % 58u));
        record->writing[offset] = (uint16_t)(38u + ((index + (uint64_t)offset * 5u + school_slot * 7u) % 62u));
    }
    record->tuition_band = 1000u + (uint32_t)((school_slot * 37u + index) % 5000u);
    record->scholarship_amount = 500u + (uint32_t)((index * 17u + school_slot * 19u) % 8000u);
    record->outstanding_balance = 250u + (uint32_t)((index * 23u + school_slot * 31u) % 10000u);
    record->household_income = 25000u + (uint32_t)((index * 97u + school_slot * 101u) % 150000u);
    record->compliance_flags = mix32((uint32_t)(index * 97u + school_slot * 389u)) & 0xFFFFu;
}

static void populate_student_record(
    StudentRecord *record,
    const DiskRecord *disk,
    SchoolBase **school_table,
    int school_capacity)
{
    SchoolBase **school_slot = load_school_slot(school_table, school_capacity, disk->school_id);
    if (*school_slot == NULL)
    {
        *school_slot = make_school(disk);
    }
    memset(record, 0, sizeof(*record));
    record->school = *school_slot;
    record->student_id = disk->student_id;
    record->cohort = disk->cohort;
    record->postcode = disk->postcode;
    record->grade_level = disk->grade_level;
    record->program_track = disk->program_track;
    record->support_bits = disk->support_bits;
    record->guardian_count = disk->guardian_count;
    memcpy(record->tags, disk->tags, sizeof(record->tags));
    record->attendance_present = disk->attendance_present;
    record->attendance_absent = disk->attendance_absent;
    record->attendance_late = disk->attendance_late;
    memcpy(record->maths, disk->maths, sizeof(record->maths));
    memcpy(record->reading, disk->reading, sizeof(record->reading));
    memcpy(record->science, disk->science, sizeof(record->science));
    memcpy(record->writing, disk->writing, sizeof(record->writing));
    record->tuition_band = disk->tuition_band;
    record->scholarship_amount = disk->scholarship_amount;
    record->outstanding_balance = disk->outstanding_balance;
    record->household_income = disk->household_income;
    record->compliance_flags = disk->compliance_flags;
}

static List *load_records(const char *path)
{
    FILE *handle = fopen(path, "rb");
    if (handle == NULL)
    {
        fprintf(stderr, "failed to open %s\n", path);
        return NULL;
    }

    FileHeader header;
    if (fread(&header, sizeof(header), 1, handle) != 1)
    {
        fprintf(stderr, "failed to read header\n");
        fclose(handle);
        return NULL;
    }
    if (memcmp(header.magic, MAGIC, sizeof(header.magic)) != 0 || header.version != VERSION)
    {
        fprintf(stderr, "unsupported dataset format\n");
        fclose(handle);
        return NULL;
    }

    int count = (int)header.count;
    List *records = init_list_with_capacity(count > 0 ? count : 2);
    int school_capacity = 8192;
    SchoolBase **school_table = calloc((size_t)school_capacity, sizeof(*school_table));

    for (int index = 0; index < count; ++index)
    {
        DiskRecord disk;
        if (fread(&disk, sizeof(disk), 1, handle) != 1)
        {
            fprintf(stderr, "failed to read record %d\n", index);
            fclose(handle);
            return NULL;
        }

        SchoolBase **school_slot = load_school_slot(school_table, school_capacity, disk.school_id);
        if (*school_slot == NULL)
        {
            *school_slot = make_school(&disk);
        }

        StudentRecord *record = malloc(sizeof(*record));
        memset(record, 0, sizeof(*record));
        record->school = *school_slot;
        record->student_id = disk.student_id;
        record->cohort = disk.cohort;
        record->postcode = disk.postcode;
        record->grade_level = disk.grade_level;
        record->program_track = disk.program_track;
        record->support_bits = disk.support_bits;
        record->guardian_count = disk.guardian_count;
        memcpy(record->tags, disk.tags, sizeof(record->tags));
        record->attendance_present = disk.attendance_present;
        record->attendance_absent = disk.attendance_absent;
        record->attendance_late = disk.attendance_late;
        memcpy(record->maths, disk.maths, sizeof(record->maths));
        memcpy(record->reading, disk.reading, sizeof(record->reading));
        memcpy(record->science, disk.science, sizeof(record->science));
        memcpy(record->writing, disk.writing, sizeof(record->writing));
        record->tuition_band = disk.tuition_band;
        record->scholarship_amount = disk.scholarship_amount;
        record->outstanding_balance = disk.outstanding_balance;
        record->household_income = disk.household_income;
        record->compliance_flags = disk.compliance_flags;
        push_back(records, record);
    }

    free(school_table);
    fclose(handle);
    return records;
}

static List *clone_record_list(List *records)
{
    List *copy = init_list_with_capacity(records->size > 0 ? records->size : 2);
    for (int index = 0; index < records->size; ++index)
    {
        push_back(copy, list_get(records, index));
    }
    return copy;
}

static List *clone_record_list_from_offset(List *records, int offset)
{
    int size = records->size - offset;
    List *copy = init_list_with_capacity(size > 0 ? size : 2);
    for (int index = offset; index < records->size; ++index)
    {
        push_back(copy, list_get(records, index));
    }
    return copy;
}

static void **build_zipped_record_buffer(List *records, intptr_t *count_out)
{
    intptr_t count = records->size >= 4 ? (intptr_t)(records->size - 3) : 0;
    void **buffer = count > 0 ? calloc((size_t)count, sizeof(*buffer)) : NULL;
    if (count_out != NULL)
    {
        *count_out = count;
    }
    for (intptr_t index = 0; index < count; ++index)
    {
        ZippedRecord *combined = malloc(sizeof(*combined));
        if (combined == NULL)
        {
            continue;
        }
        memset(combined, 0, sizeof(*combined));
        combined->primary = (StudentRecord *)list_get(records, (int)index);
        combined->secondary = (StudentRecord *)list_get(records, (int)index + 1);
        combined->tertiary = (StudentRecord *)list_get(records, (int)index + 2);
        combined->quaternary = (StudentRecord *)list_get(records, (int)index + 3);
        buffer[index] = combined;
    }
    return buffer;
}

static void free_zipped_record_buffer(void **buffer, intptr_t count)
{
    if (buffer == NULL)
    {
        return;
    }
    for (intptr_t index = 0; index < count; ++index)
    {
        free(buffer[index]);
    }
    free(buffer);
}

static void *combine_zipped_records(void *raw)
{
    PairValue *outer = (PairValue *)raw;
    PairValue *left_pair = (PairValue *)pair_left(outer);
    PairValue *right_pair = (PairValue *)pair_right(outer);
    ZippedRecord *combined = malloc(sizeof(*combined));
    memset(combined, 0, sizeof(*combined));
    combined->primary = (StudentRecord *)pair_left(left_pair);
    combined->secondary = (StudentRecord *)pair_right(left_pair);
    combined->tertiary = (StudentRecord *)pair_left(right_pair);
    combined->quaternary = (StudentRecord *)pair_right(right_pair);
    return combined;
}

static void populate_enriched_record(EnrichedRecord *enriched, const ZippedRecord *zipped, bool include_strings)
{
    StudentRecord *record = zipped->primary;
    StudentRecord *secondary = zipped->secondary;
    StudentRecord *tertiary = zipped->tertiary;
    StudentRecord *quaternary = zipped->quaternary;
    memset(enriched, 0, sizeof(*enriched));
    enriched->school = record->school;
    enriched->student_id = record->student_id;
    enriched->cohort = record->cohort;
    enriched->postcode = record->postcode;
    enriched->grade_level = record->grade_level;
    enriched->program_track = record->program_track;
    enriched->guardian_count = (uint16_t)(record->guardian_count + secondary->guardian_count + tertiary->guardian_count + quaternary->guardian_count);
    enriched->attendance_rate = (uint16_t)(
        ((uint32_t)(record->attendance_present + secondary->attendance_present + tertiary->attendance_present + quaternary->attendance_present) * 10000u) /
        (uint32_t)(((record->attendance_present + secondary->attendance_present + tertiary->attendance_present + quaternary->attendance_present) +
            (record->attendance_absent + secondary->attendance_absent + tertiary->attendance_absent + quaternary->attendance_absent) +
            (record->attendance_late + secondary->attendance_late + tertiary->attendance_late + quaternary->attendance_late)) > 0
            ? ((record->attendance_present + secondary->attendance_present + tertiary->attendance_present + quaternary->attendance_present) +
                (record->attendance_absent + secondary->attendance_absent + tertiary->attendance_absent + quaternary->attendance_absent) +
                (record->attendance_late + secondary->attendance_late + tertiary->attendance_late + quaternary->attendance_late))
            : 1));
    enriched->attendance_absent = (uint16_t)(record->attendance_absent + secondary->attendance_absent + tertiary->attendance_absent + quaternary->attendance_absent);
    enriched->attendance_late = (uint16_t)(record->attendance_late + secondary->attendance_late + tertiary->attendance_late + quaternary->attendance_late);
    enriched->scholarship_amount = record->scholarship_amount + secondary->scholarship_amount + tertiary->scholarship_amount + quaternary->scholarship_amount;
    enriched->outstanding_balance = record->outstanding_balance + secondary->outstanding_balance + tertiary->outstanding_balance + quaternary->outstanding_balance;
    enriched->averages[0] = weighted_average(record->maths) + weighted_average(secondary->maths);
    enriched->averages[1] = weighted_average(record->reading) + weighted_average(tertiary->reading);
    enriched->averages[2] = weighted_average(record->science) + weighted_average(quaternary->science);
    enriched->averages[3] = weighted_average(record->writing) + weighted_average(secondary->writing);
    enriched->composite = (enriched->averages[0] * 35 + enriched->averages[1] * 25 +
        enriched->averages[2] * 20 + enriched->averages[3] * 20) / 100;
    enriched->aid_score = (int32_t)record->scholarship_amount - (int32_t)(record->outstanding_balance / 2u);
    enriched->support_score =
        ((record->support_bits & 0x01u) ? 1 : 0) +
        ((secondary->support_bits & 0x02u) ? 1 : 0) +
        ((tertiary->support_bits & 0x04u) ? 1 : 0) +
        ((quaternary->support_bits & 0x08u) ? 1 : 0);
    enriched->tag_vector[0] = record->tags[0];
    enriched->tag_vector[1] = record->tags[1];
    enriched->tag_vector[2] = record->tags[2];
    enriched->tag_vector[3] = record->tags[3];
    enriched->tag_vector[4] = secondary->tags[0];
    enriched->tag_vector[5] = secondary->tags[1];
    enriched->tag_vector[6] = tertiary->tags[0];
    enriched->tag_vector[7] = tertiary->tags[1];
    enriched->tag_vector[8] = quaternary->tags[0];
    enriched->tag_vector[9] = quaternary->tags[1];
    enriched->tag_vector[10] = (uint16_t)((record->school->region_code + secondary->school->region_code + tertiary->school->region_code + quaternary->school->region_code) & 31u);
    enriched->tag_vector[11] = (uint16_t)((record->support_bits ^ secondary->program_track ^ tertiary->cohort ^ quaternary->grade_level) & 31u);
    enriched->profile_hash = mix32(
        (record->student_id ^ secondary->student_id ^ tertiary->student_id ^ quaternary->student_id) ^
        ((uint32_t)enriched->composite << 1) ^
        ((uint32_t)enriched->attendance_rate << 2) ^
        ((uint32_t)(enriched->aid_score + (int32_t)secondary->scholarship_amount - (int32_t)(secondary->outstanding_balance / 2u)) << 3) ^
        ((uint32_t)record->grade_level << 7) ^
        ((uint32_t)(record->program_track ^ secondary->program_track ^ tertiary->program_track ^ quaternary->program_track) << 11) ^
        ((uint32_t)(record->cohort ^ secondary->cohort ^ tertiary->cohort ^ quaternary->cohort) << 13));
    if (include_strings)
    {
        snprintf(
            enriched->school_key,
            sizeof(enriched->school_key),
            "school:%u:%u:%u:%u:%u:%u",
            record->school->kind,
            record->school->region_code,
            record->school->district_code,
            record->school->school_id,
            secondary->school->school_id,
            tertiary->school->school_id);
    }
}

static void *enrich_record(void *raw)
{
    ZippedRecord *zipped = (ZippedRecord *)raw;
    EnrichedRecord *enriched = malloc(sizeof(*enriched));
    populate_enriched_record(enriched, zipped, true);
    return enriched;
}

static DatabaseState *init_database_state(void)
{
    DatabaseState *state = malloc(sizeof(*state));
    state->capacity = 8192;
    state->table = calloc((size_t)state->capacity, sizeof(*state->table));
    state->last_touched = NULL;
    state->last_enriched = NULL;
    return state;
}

static void reset_database_state(DatabaseState *state)
{
    if (state == NULL)
    {
        return;
    }
    memset(state->table, 0, (size_t)state->capacity * sizeof(*state->table));
    state->last_touched = NULL;
    state->last_enriched = NULL;
}

static SchoolAggregate *find_school_aggregate(DatabaseState *state, uint32_t school_id)
{
    uint32_t slot = school_hash(school_id) & (uint32_t)(state->capacity - 1);
    for (;;)
    {
        SchoolAggregate *entry = &state->table[slot];
        if (!entry->used || entry->school->school_id == school_id)
        {
            return entry;
        }
        slot = (slot + 1u) & (uint32_t)(state->capacity - 1);
    }
}

static void *update_database_state(void *raw_accum, void *raw_next)
{
    DatabaseState *state = raw_accum != NULL ? (DatabaseState *)raw_accum : init_database_state();
    EnrichedRecord *enriched = (EnrichedRecord *)raw_next;
    SchoolAggregate *aggregate = find_school_aggregate(state, enriched->school->school_id);
    if (!aggregate->used)
    {
        memset(aggregate, 0, sizeof(*aggregate));
        aggregate->used = true;
        aggregate->school = enriched->school;
    }

    int special_index = school_special_index(enriched->school);
    aggregate->active_count += 1u;
    aggregate->scholarship_count += enriched->scholarship_amount > 0u ? 1u : 0u;
    aggregate->support_count += enriched->support_score >= 2 ? 1u : 0u;
    aggregate->composite_sum += (uint64_t)enriched->composite;
    aggregate->attendance_sum += (uint64_t)enriched->attendance_rate;
    aggregate->funding_sum += (int64_t)enriched->outstanding_balance -
        (int64_t)(enriched->scholarship_amount / 2u) + special_index;
    aggregate->compliance_mix = mix32(aggregate->compliance_mix ^ enriched->profile_hash ^ (uint32_t)special_index);
    for (int index = 0; index < EXTENDED_TAG_COUNT; ++index)
    {
        aggregate->tag_hist[enriched->tag_vector[index] & 31u] += 1u;
    }

    state->last_touched = aggregate;
    state->last_enriched = enriched;
    return state;
}

static void choose_top_buckets(uint32_t out[TOP_BUCKETS], const uint32_t hist[HIST_BUCKETS])
{
    bool used[HIST_BUCKETS];
    memset(used, 0, sizeof(used));
    for (int index = 0; index < TOP_BUCKETS; ++index)
    {
        uint32_t best_bucket = 0;
        bool found = false;
        for (uint32_t bucket = 0; bucket < HIST_BUCKETS; ++bucket)
        {
            if (used[bucket])
            {
                continue;
            }
            if (!found ||
                hist[bucket] > hist[best_bucket] ||
                (hist[bucket] == hist[best_bucket] && bucket < best_bucket))
            {
                best_bucket = bucket;
                found = true;
            }
        }
        out[index] = best_bucket;
        used[best_bucket] = true;
    }
}

static void populate_school_snapshot(SchoolSnapshot *snapshot, DatabaseState *state, bool include_strings)
{
    SchoolAggregate *aggregate = state->last_touched;
    EnrichedRecord *enriched = state->last_enriched;
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->school = aggregate->school;
    snapshot->active_count = aggregate->active_count;
    snapshot->scholarship_count = aggregate->scholarship_count;
    snapshot->support_count = aggregate->support_count;
    snapshot->avg_composite = (uint32_t)(aggregate->composite_sum / aggregate->active_count);
    snapshot->avg_attendance = (uint32_t)(aggregate->attendance_sum / aggregate->active_count);
    snapshot->funding_pressure = (int32_t)floor_div_i64(
        aggregate->funding_sum,
        (int64_t)aggregate->active_count);
    {
        int special_index = school_special_index(snapshot->school);
        int absent_pressure = (int)enriched->attendance_absent * 37 + (int)enriched->attendance_late * 19;
        int risk_score = 15000 - (int)snapshot->avg_composite * 2 + (int)floor_div_i64(snapshot->funding_pressure, 4) +
            (int)snapshot->support_count * 13 + absent_pressure + special_index * 5 +
            (aggregate->compliance_mix & 127u);
        snapshot->risk_score = (uint32_t)(risk_score > 0 ? risk_score : 0);
    }
    snapshot->risk_tier = snapshot->risk_score >= 9000u ? 2u : snapshot->risk_score >= 7000u ? 1u : 0u;
    choose_top_buckets(snapshot->top_buckets, aggregate->tag_hist);
    if (include_strings)
    {
        snprintf(
            snapshot->export_row,
            sizeof(snapshot->export_row),
            "%u|%u|%u|%u|%u|%u",
            snapshot->school->kind,
            snapshot->school->region_code,
            snapshot->school->school_id,
            snapshot->risk_tier,
            snapshot->active_count,
            snapshot->avg_composite);
    }
    for (int index = 0; index < 8; ++index)
    {
        snapshot->alert_flags[index] = (snapshot->risk_score >> (index * 2)) & 3u;
    }
    snapshot->signature = mix32(
        snapshot->school->school_id ^
        (snapshot->risk_tier << 6) ^
        (snapshot->active_count << 9) ^
        (snapshot->avg_composite << 1) ^
        (snapshot->avg_attendance << 3) ^
        ((uint32_t)snapshot->funding_pressure << 5) ^
        snapshot->top_buckets[0] ^
        (snapshot->top_buckets[1] << 8));
}

static void *build_school_snapshot(void *raw_state)
{
    DatabaseState *state = (DatabaseState *)raw_state;
    SchoolSnapshot *snapshot = malloc(sizeof(*snapshot));
    populate_school_snapshot(snapshot, state, true);
    return snapshot;
}

static void enrich_record_into(void *raw_out, void *raw)
{
    populate_enriched_record((EnrichedRecord *)raw_out, (ZippedRecord *)raw, false);
}

static void enrich_record_window_into(void *raw_out, void *raw)
{
    void **window = (void **)raw;
    ZippedRecord zipped;
    memset(&zipped, 0, sizeof(zipped));
    zipped.primary = (StudentRecord *)window[0];
    zipped.secondary = (StudentRecord *)window[1];
    zipped.tertiary = (StudentRecord *)window[2];
    zipped.quaternary = (StudentRecord *)window[3];
    populate_enriched_record((EnrichedRecord *)raw_out, &zipped, false);
}

static void update_database_state_mut(void *raw_accum, void *raw_next)
{
    DatabaseState *state = (DatabaseState *)raw_accum;
    EnrichedRecord *enriched = (EnrichedRecord *)raw_next;
    SchoolAggregate *aggregate = find_school_aggregate(state, enriched->school->school_id);
    if (!aggregate->used)
    {
        memset(aggregate, 0, sizeof(*aggregate));
        aggregate->used = true;
        aggregate->school = enriched->school;
    }

    {
        int special_index = school_special_index(enriched->school);
        aggregate->active_count += 1u;
        aggregate->scholarship_count += enriched->scholarship_amount > 0u ? 1u : 0u;
        aggregate->support_count += enriched->support_score >= 2 ? 1u : 0u;
        aggregate->composite_sum += (uint64_t)enriched->composite;
        aggregate->attendance_sum += (uint64_t)enriched->attendance_rate;
        aggregate->funding_sum += (int64_t)enriched->outstanding_balance -
            (int64_t)(enriched->scholarship_amount / 2u) + special_index;
        aggregate->compliance_mix = mix32(aggregate->compliance_mix ^ enriched->profile_hash ^ (uint32_t)special_index);
    }
    for (int index = 0; index < EXTENDED_TAG_COUNT; ++index)
    {
        aggregate->tag_hist[enriched->tag_vector[index] & 31u] += 1u;
    }

    state->last_touched = aggregate;
    state->last_enriched = enriched;
}

static void build_school_snapshot_into(void *raw_out, void *raw_state)
{
    populate_school_snapshot((SchoolSnapshot *)raw_out, (DatabaseState *)raw_state, false);
}

static bool snapshot_is_relevant(void *raw)
{
    SchoolSnapshot *snapshot = (SchoolSnapshot *)raw;
    return snapshot->active_count >= 2u &&
        (snapshot->risk_tier > 0u || snapshot->funding_pressure > 100);
}

static void *snapshot_signature(void *raw)
{
    SchoolSnapshot *snapshot = (SchoolSnapshot *)raw;
    return (void *)(uintptr_t)snapshot->signature;
}

static bool snapshot_is_cold(void *raw)
{
    SchoolSnapshot *snapshot = (SchoolSnapshot *)raw;
    return snapshot->active_count < 3u;
}

static void *accumulate_digest(void *raw_accum, void *raw_next)
{
    AuditDigest *digest = raw_accum != NULL ? (AuditDigest *)raw_accum : calloc(1, sizeof(*digest));
    SchoolSnapshot *snapshot = (SchoolSnapshot *)raw_next;
    digest->emitted_snapshots += 1u;
    digest->total_enrollment += snapshot->active_count;
    digest->total_risk += snapshot->risk_score;
    if (snapshot->risk_tier > digest->max_risk_tier)
    {
        digest->max_risk_tier = snapshot->risk_tier;
    }
    digest->last_school_id = snapshot->school->school_id;
    digest->digest = mix32(
        digest->digest ^
        mix32(snapshot->school->school_id) ^
        mix32(snapshot->active_count) ^
        mix32(snapshot->risk_score) ^
        mix32((uint32_t)snapshot->funding_pressure) ^
        mix32(snapshot->risk_tier));
    return digest;
}

static void accumulate_digest_mut(void *raw_accum, void *raw_next)
{
    AuditDigest *digest = (AuditDigest *)raw_accum;
    SchoolSnapshot *snapshot = (SchoolSnapshot *)raw_next;
    digest->emitted_snapshots += 1u;
    digest->total_enrollment += snapshot->active_count;
    digest->total_risk += snapshot->risk_score;
    if (snapshot->risk_tier > digest->max_risk_tier)
    {
        digest->max_risk_tier = snapshot->risk_tier;
    }
    digest->last_school_id = snapshot->school->school_id;
    digest->digest = mix32(
        digest->digest ^
        mix32(snapshot->school->school_id) ^
        mix32(snapshot->active_count) ^
        mix32(snapshot->risk_score) ^
        mix32((uint32_t)snapshot->funding_pressure) ^
        mix32(snapshot->risk_tier));
}

static void store_result(void *value)
{
    g_result = (AuditDigest *)value;
}

static void print_result(const AuditDigest *digest, double average_ms, int runs)
{
    AuditDigest empty = {0};
    const AuditDigest *result = digest != NULL ? digest : &empty;
    printf(
        "{\"digest\": %u, \"emitted_snapshots\": %" PRIu64 ", \"total_enrollment\": %" PRIu64
        ", \"total_risk\": %" PRIu64 ", \"max_risk_tier\": %u, \"last_school_id\": %u, \"average_ms\": %.5f, \"runs\": %d}\n",
        result->digest,
        result->emitted_snapshots,
        result->total_enrollment,
        result->total_risk,
        result->max_risk_tier,
        result->last_school_id,
        average_ms,
        runs);
}

static double diff_seconds(struct timespec start, struct timespec end)
{
    return (double)(end.tv_sec - start.tv_sec) + (double)(end.tv_nsec - start.tv_nsec) / 1000000000.0;
}

static void render_progress(uint64_t processed, uint64_t total, struct timespec started_at)
{
    double percent;
    double elapsed;
    double velocity_m_items;
    int width = 30;
    int filled;
    char bar[31];
    struct timespec now;
    if (total == 0u)
    {
        total = 1u;
    }
    percent = ((double)processed * 100.0) / (double)total;
    if (percent > 100.0)
    {
        percent = 100.0;
    }
    filled = (int)((percent / 100.0) * width);
    if (filled > width)
    {
        filled = width;
    }
    for (int index = 0; index < width; ++index)
    {
        bar[index] = index < filled ? '#' : '-';
    }
    bar[width] = '\0';
    clock_gettime(CLOCK_MONOTONIC, &now);
    elapsed = diff_seconds(started_at, now);
    velocity_m_items = elapsed > 0.0 ? ((double)processed / 1000000.0) / elapsed : 0.0;
    fprintf(stderr, "\r  C progress  [%s] %.2f%% | %.2f M items/s", bar, percent, velocity_m_items);
    fflush(stderr);
}

static AuditDigest *run_synthetic_stream(uint64_t count)
{
    if (count < 4u)
    {
        return calloc(1, sizeof(AuditDigest));
    }

    int school_capacity = 8192;
    int school_count = synthetic_school_count_for(count);
    SchoolBase **school_table = calloc((size_t)school_capacity, sizeof(*school_table));
    StudentRecord window[4];
    DiskRecord disk;
    DatabaseState *state = NULL;
    AuditDigest *digest = NULL;
    bool started = false;
    bool has_last_signature = false;
    uintptr_t last_signature = 0;
    uint64_t total_items = count - 3u;
    uint64_t last_shown = UINT64_MAX;
    uint64_t batch_size = 1000000u;
    struct timespec started_at;
    clock_gettime(CLOCK_MONOTONIC, &started_at);

    for (uint64_t index = 0; index < 4u; ++index)
    {
        build_disk_record(index, school_count, &disk);
        populate_student_record(&window[index], &disk, school_table, school_capacity);
    }

    for (uint64_t batch_start = 0; batch_start + 3u < count; batch_start += batch_size)
    {
        uint64_t batch_end = batch_start + batch_size;
        if (batch_end > count - 3u)
        {
            batch_end = count - 3u;
        }
        for (uint64_t base = batch_start; base < batch_end; ++base)
        {
            EnrichedRecord enriched;
            SchoolSnapshot snapshot;
            ZippedRecord zipped;
            uintptr_t signature;
            int idx0 = (int)(base % 4u);
            int idx1 = (int)((base + 1u) % 4u);
            int idx2 = (int)((base + 2u) % 4u);
            int idx3 = (int)((base + 3u) % 4u);
            zipped.primary = &window[idx0];
            zipped.secondary = &window[idx1];
            zipped.tertiary = &window[idx2];
            zipped.quaternary = &window[idx3];

            populate_enriched_record(&enriched, &zipped, false);
            state = (DatabaseState *)update_database_state(state, &enriched);
            state->last_enriched = &enriched;
            populate_school_snapshot(&snapshot, state, false);

            if (snapshot_is_relevant(&snapshot))
            {
                signature = (uintptr_t)snapshot_signature(&snapshot);
                if (!has_last_signature || signature != last_signature)
                {
                    last_signature = signature;
                    has_last_signature = true;
                    if (started || !snapshot_is_cold(&snapshot))
                    {
                        started = true;
                        digest = (AuditDigest *)accumulate_digest(digest, &snapshot);
                    }
                }
            }

            if (base + 4u < count)
            {
                build_disk_record(base + 4u, school_count, &disk);
                populate_student_record(&window[idx0], &disk, school_table, school_capacity);
            }

            {
                uint64_t processed = base + 1u;
                uint64_t basis_points = (processed * 10000u) / total_items;
                if (basis_points != last_shown)
                {
                    uint64_t coarse_points = basis_points / 10u;
                    if (coarse_points != last_shown)
                    {
                        last_shown = coarse_points;
                        render_progress(processed, total_items, started_at);
                    }
                }
            }
        }
    }

    render_progress(total_items, total_items, started_at);
    fprintf(stderr, "\n");
    free(school_table);
    return digest != NULL ? digest : calloc(1, sizeof(AuditDigest));
}

#ifndef EDU_COMPLEX_SHARED_ONLY
int main(int argc, char **argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "usage: edu_complex_benchmark <data.bin> [runs]\n");
        fprintf(stderr, "   or: edu_complex_benchmark --synthetic <count> [runs]\n");
        return 1;
    }

    long long total_ns = 0;
    int runs = 3;

    if (strcmp(argv[1], "--synthetic") == 0)
    {
        uint64_t synthetic_count;
        if (argc < 3)
        {
            fprintf(stderr, "missing synthetic count\n");
            return 1;
        }
        synthetic_count = (uint64_t)strtoull(argv[2], NULL, 10);
        runs = argc > 3 ? atoi(argv[3]) : 1;
        for (int run = 0; run < runs; ++run)
        {
            struct timespec start;
            struct timespec end;
            clock_gettime(CLOCK_MONOTONIC, &start);
            g_result = run_synthetic_stream(synthetic_count);
            clock_gettime(CLOCK_MONOTONIC, &end);
            total_ns += (long long)(end.tv_sec - start.tv_sec) * 1000000000LL +
                (long long)(end.tv_nsec - start.tv_nsec);
        }
    }
    else
    {
        List *records = load_records(argv[1]);
        runs = argc > 2 ? atoi(argv[2]) : 3;
        if (records == NULL)
        {
            return 1;
        }

        for (int run = 0; run < runs; ++run)
        {
            Observable *stream0 = create_observable();
            Observable *stream1 = create_observable();
            Observable *stream2 = create_observable();
            Observable *stream3 = create_observable();
            Observable *left_zip;
            Observable *right_zip;
            Observable *source;
            freelist(stream0->data);
            freelist(stream1->data);
            freelist(stream2->data);
            freelist(stream3->data);
            stream0->data = clone_record_list_from_offset(records, 0);
            stream1->data = clone_record_list_from_offset(records, 1);
            stream2->data = clone_record_list_from_offset(records, 2);
            stream3->data = clone_record_list_from_offset(records, 3);
            left_zip = zip(2, stream0, stream1);
            right_zip = zip(2, stream2, stream3);
            source = zip(2, left_zip, right_zip);
            g_result = NULL;
            {
                struct timespec start;
                struct timespec end;
                clock_gettime(CLOCK_MONOTONIC, &start);
                source = pipe(
                    source,
                    8,
                    map(combine_zipped_records),
                    map(enrich_record),
                    scan(update_database_state),
                    map(build_school_snapshot),
                    filter(snapshot_is_relevant),
                    distinctUntilChanged(snapshot_signature),
                    skipWhile(snapshot_is_cold),
                    reduce(accumulate_digest));
                subscribe(source, store_result);
                clock_gettime(CLOCK_MONOTONIC, &end);
                total_ns += (long long)(end.tv_sec - start.tv_sec) * 1000000000LL +
                    (long long)(end.tv_nsec - start.tv_nsec);
            }
        }
    }

    print_result(g_result, (double)total_ns / runs / 1e6, runs);
    return 0;
}
#endif
