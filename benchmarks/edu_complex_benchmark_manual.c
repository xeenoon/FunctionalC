#define _POSIX_C_SOURCE 200809L
#define EDU_COMPLEX_SHARED_ONLY
#ifndef EDU_COMPLEX_BENCHMARK_ALREADY_INCLUDED
#include "edu_complex_benchmark.c"
#endif

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct
{
    void *storage_block;
    List *records;
    StudentRecord *record_pool;
    DerivedSchool *school_pool;
    SchoolBase **school_table;
    uint32_t school_capacity;
    uint32_t school_count;
} ManualDataset;

typedef struct
{
    void *storage_block;
    void **items;
    ZippedRecord *records;
    intptr_t count;
} ManualZippedBuffer;

typedef struct
{
    bool used;
    SchoolBase *school;
    int special_index;
    uint32_t active_count;
    uint32_t scholarship_count;
    uint32_t support_count;
    uint64_t composite_sum;
    uint64_t attendance_sum;
    int64_t funding_sum;
    uint32_t compliance_mix;
    uint32_t tag_hist[HIST_BUCKETS];
} ManualAggregate;

typedef struct
{
    void *storage_block;
    ManualAggregate *aggregates;
    uint32_t base_school_id;
    uint32_t aggregate_count;
} ManualContext;

static AuditDigest manual_result;

static size_t align_up_size(size_t value, size_t alignment)
{
    size_t mask = alignment - 1u;
    return (value + mask) & ~mask;
}

static uint32_t next_power_of_two_u32(uint32_t value)
{
    uint32_t result = 1u;
    while (result < value)
    {
        result <<= 1u;
    }
    return result;
}

static void init_derived_school(DerivedSchool *school, const DiskRecord *record)
{
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
}

static void populate_student_record_from_disk(StudentRecord *record, const DiskRecord *disk, SchoolBase *school)
{
    record->school = school;
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

static ManualDataset *load_manual_dataset(const char *path)
{
    FILE *handle = fopen(path, "rb");
    FileHeader header;
    uint32_t record_count;
    uint32_t school_capacity;
    size_t list_align = alignof(List);
    size_t ptr_align = alignof(void *);
    size_t record_align = alignof(StudentRecord);
    size_t school_align = alignof(DerivedSchool);
    size_t total_size;
    size_t cursor;
    void *storage;
    ManualDataset *dataset;

    if (handle == NULL)
    {
        fprintf(stderr, "failed to open %s\n", path);
        return NULL;
    }

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

    record_count = (uint32_t)header.count;
    school_capacity = next_power_of_two_u32(record_count > 0u ? record_count * 2u : 2u);

    /* OPT_REUSE_1:
     * Reuse plan: the manual path loads the entire dataset once into a single immutable region:
     *   [ManualDataset][List][record pointer table][StudentRecord slab][school hash table][DerivedSchool slab].
     * Every pointer in the benchmark becomes an offset into that region, so there is no per-record or per-school
     * allocation left to perform once loading starts.
     * Why reuse is safe: StudentRecord and School objects are read-only after load, and every benchmark run only
     * reads them. The whole dataset has one owner and one lifetime: process startup to benchmark shutdown.
     * Compiler plan summary:
     *   1. Detect N identical heap allocations inside the loader loop.
     *   2. Prove the allocated objects escape only through one owning container with run-long lifetime.
     *   3. Hoist capacity sizing from the file header, replace object-by-object malloc with one region allocation.
     *   4. Rewrite each escaping pointer as an address within the region.
     */
    total_size = align_up_size(sizeof(ManualDataset), list_align);
    total_size = align_up_size(total_size + sizeof(List), ptr_align);
    total_size += (size_t)(record_count > 0u ? record_count : 2u) * sizeof(void *);
    total_size = align_up_size(total_size, record_align);
    total_size += (size_t)(record_count > 0u ? record_count : 1u) * sizeof(StudentRecord);
    total_size = align_up_size(total_size, ptr_align);
    total_size += (size_t)school_capacity * sizeof(SchoolBase *);
    total_size = align_up_size(total_size, school_align);
    total_size += (size_t)(record_count > 0u ? record_count : 1u) * sizeof(DerivedSchool);

    storage = calloc(1, total_size);
    if (storage == NULL)
    {
        fclose(handle);
        return NULL;
    }

    dataset = (ManualDataset *)storage;
    dataset->storage_block = storage;
    cursor = align_up_size(sizeof(ManualDataset), list_align);
    dataset->records = (List *)((char *)storage + cursor);
    cursor = align_up_size(cursor + sizeof(List), ptr_align);
    dataset->records->data = (void **)((char *)storage + cursor);
    dataset->records->payload_block = storage;
    dataset->records->front = 0;
    dataset->records->rear = (int)record_count;
    dataset->records->allocatedsize = (int)(record_count > 0u ? record_count : 2u);
    dataset->records->size = (int)record_count;
    cursor += (size_t)(record_count > 0u ? record_count : 2u) * sizeof(void *);
    cursor = align_up_size(cursor, record_align);
    dataset->record_pool = (StudentRecord *)((char *)storage + cursor);
    cursor += (size_t)(record_count > 0u ? record_count : 1u) * sizeof(StudentRecord);
    cursor = align_up_size(cursor, ptr_align);
    dataset->school_table = (SchoolBase **)((char *)storage + cursor);
    dataset->school_capacity = school_capacity;
    cursor += (size_t)school_capacity * sizeof(SchoolBase *);
    cursor = align_up_size(cursor, school_align);
    dataset->school_pool = (DerivedSchool *)((char *)storage + cursor);

    for (uint32_t index = 0; index < record_count; ++index)
    {
        DiskRecord disk;
        SchoolBase **school_slot;
        DerivedSchool *school;
        StudentRecord *record;

        if (fread(&disk, sizeof(disk), 1, handle) != 1)
        {
            fprintf(stderr, "failed to read record %u\n", index);
            fclose(handle);
            free(storage);
            return NULL;
        }

        school_slot = load_school_slot(dataset->school_table, (int)dataset->school_capacity, disk.school_id);
        if (*school_slot == NULL)
        {
            if (dataset->school_count >= record_count)
            {
                fprintf(stderr, "school pool exhausted\n");
                fclose(handle);
                free(storage);
                return NULL;
            }
            school = &dataset->school_pool[dataset->school_count++];
            init_derived_school(school, &disk);
            *school_slot = &school->base;
        }

        record = &dataset->record_pool[index];
        populate_student_record_from_disk(record, &disk, *school_slot);
        dataset->records->data[index] = record;
    }

    fclose(handle);
    return dataset;
}

static void free_manual_dataset(ManualDataset *dataset)
{
    if (dataset == NULL)
    {
        return;
    }
    free(dataset->storage_block);
}

static ManualZippedBuffer *build_manual_zipped_buffer(List *records)
{
    intptr_t count = records != NULL && records->size >= 4 ? (intptr_t)(records->size - 3) : 0;
    size_t total_size;
    void *storage;
    ManualZippedBuffer *buffer;
    size_t cursor;

    /* OPT_REUSE_4:
     * Reuse plan: build the zipped input once as one contiguous slab instead of N heap-allocated ZippedRecord
     * objects. The planner loop then reads stable pointers from this immutable slab and does no zip-shape
     * reconstruction inside the timed section.
     * Why reuse is safe: each ZippedRecord is immutable after build and only referenced by the benchmark harness.
     * Compiler plan summary:
     *   1. Detect repeated fixed-width window/object construction with run-long lifetime.
     *   2. Hoist construction out of the timed loop.
     *   3. Replace per-object heap allocation with one contiguous block.
     *   4. Rewrite stream inputs as pointers into the slab.
     */
    total_size = align_up_size(sizeof(ManualZippedBuffer), alignof(void *));
    total_size += (size_t)(count > 0 ? count : 1) * sizeof(void *);
    total_size = align_up_size(total_size, alignof(ZippedRecord));
    total_size += (size_t)(count > 0 ? count : 1) * sizeof(ZippedRecord);
    storage = calloc(1, total_size);
    if (storage == NULL)
    {
        return NULL;
    }

    buffer = (ManualZippedBuffer *)storage;
    buffer->storage_block = storage;
    buffer->count = count;
    cursor = align_up_size(sizeof(ManualZippedBuffer), alignof(void *));
    buffer->items = (void **)((char *)storage + cursor);
    cursor += (size_t)(count > 0 ? count : 1) * sizeof(void *);
    cursor = align_up_size(cursor, alignof(ZippedRecord));
    buffer->records = (ZippedRecord *)((char *)storage + cursor);

    for (intptr_t index = 0; index < count; ++index)
    {
        ZippedRecord *zipped = &buffer->records[index];
        zipped->primary = (StudentRecord *)records->data[index];
        zipped->secondary = (StudentRecord *)records->data[index + 1];
        zipped->tertiary = (StudentRecord *)records->data[index + 2];
        zipped->quaternary = (StudentRecord *)records->data[index + 3];
        buffer->items[index] = zipped;
    }
    return buffer;
}

static void free_manual_zipped_buffer(ManualZippedBuffer *buffer)
{
    if (buffer == NULL)
    {
        return;
    }
    free(buffer->storage_block);
}

static void choose_top_two_buckets_manual(const uint32_t hist[HIST_BUCKETS], uint32_t *top0, uint32_t *top1)
{
    uint32_t best0 = 0;
    uint32_t best1 = 1;
    if (hist[best1] > hist[best0] || (hist[best1] == hist[best0] && best1 < best0))
    {
        uint32_t swap = best0;
        best0 = best1;
        best1 = swap;
    }

    for (uint32_t bucket = 2; bucket < HIST_BUCKETS; ++bucket)
    {
        if (hist[bucket] > hist[best0] || (hist[bucket] == hist[best0] && bucket < best0))
        {
            best1 = best0;
            best0 = bucket;
        }
        else if (bucket != best0 &&
            (hist[bucket] > hist[best1] || (hist[bucket] == hist[best1] && bucket < best1)))
        {
            best1 = bucket;
        }
    }

    *top0 = best0;
    *top1 = best1;
}

static ManualContext *build_manual_context(List *records)
{
    uint32_t min_school_id;
    uint32_t max_school_id;
    size_t total_size;
    void *storage;
    ManualContext *context;

    if (records == NULL || records->size <= 0)
    {
        return NULL;
    }

    StudentRecord *first = (StudentRecord *)records->data[0];
    min_school_id = first->school->school_id;
    max_school_id = first->school->school_id;
    for (int index = 1; index < records->size; ++index)
    {
        StudentRecord *record = (StudentRecord *)records->data[index];
        if (record->school->school_id < min_school_id)
        {
            min_school_id = record->school->school_id;
        }
        if (record->school->school_id > max_school_id)
        {
            max_school_id = record->school->school_id;
        }
    }

    /* OPT_REUSE_2:
     * Reuse plan: the aggregate table is a single run-reusable block. We allocate it once, then reset it with
     * memset before each benchmark run instead of rebuilding per-school state with fresh allocations.
     * Why reuse is safe: every aggregate is fully overwritten or zero-initialized between runs, and no aggregate
     * pointer escapes past a run boundary.
     * Compiler plan summary:
     *   1. Detect per-run allocation of a same-sized accumulator table.
     *   2. Prove the table does not escape the loop over runs.
     *   3. Sink initialization into a reset step and retain the backing storage across runs.
     *   4. Replace allocate/free pairs with one persistent arena block plus bulk clear.
     */
    total_size = align_up_size(sizeof(ManualContext), alignof(ManualAggregate));
    total_size += (size_t)(max_school_id - min_school_id + 1u) * sizeof(ManualAggregate);
    storage = calloc(1, total_size);
    if (storage == NULL)
    {
        return NULL;
    }

    context = (ManualContext *)storage;
    context->storage_block = storage;
    context->base_school_id = min_school_id;
    context->aggregate_count = max_school_id - min_school_id + 1u;
    context->aggregates = (ManualAggregate *)((char *)storage + align_up_size(sizeof(ManualContext), alignof(ManualAggregate)));
    return context;
}

static void free_manual_context(ManualContext *context)
{
    if (context == NULL)
    {
        return;
    }
    free(context->storage_block);
}

static void reset_manual_context(ManualContext *context)
{
    if (context == NULL)
    {
        return;
    }
    memset(context->aggregates, 0, (size_t)context->aggregate_count * sizeof(*context->aggregates));
}

static AuditDigest *run_manual_context(ManualContext *context, List *records)
{
    StudentRecord **items = (StudentRecord **)records->data;
    intptr_t count = records->size;
    bool started = false;
    bool has_last_signature = false;
    uint32_t last_signature = 0;

    /* OPT_REUSE_3:
     * Reuse plan: the hot loop writes into a single static digest and the persistent aggregate block only.
     * There are zero malloc/calloc calls on the steady-state path; all temporaries stay in registers or on stack.
     * Why reuse is safe: the digest is consumed only after the loop finishes, and the loop is single-threaded.
     * Compiler plan summary:
     *   1. Detect heap objects whose fields are reduced immediately and never need independent identity.
     *   2. Scalar-replace those objects into locals / persistent scratch slots.
     *   3. Hoist all remaining storage to loop-invariant arenas.
     *   4. Emit a fused loop with no dynamic allocation in the body.
     */
    memset(&manual_result, 0, sizeof(manual_result));
    if (context == NULL || records == NULL || count < 4)
    {
        return &manual_result;
    }

    for (intptr_t index = 0; index + 3 < count; ++index)
    {
        StudentRecord *record = items[index];
        StudentRecord *secondary = items[index + 1];
        StudentRecord *tertiary = items[index + 2];
        StudentRecord *quaternary = items[index + 3];
        ManualAggregate *aggregate;
        uint32_t attendance_present;
        uint32_t attendance_absent;
        uint32_t attendance_late;
        uint32_t attendance_total;
        uint16_t attendance_rate;
        uint32_t scholarship_amount;
        uint32_t outstanding_balance;
        int32_t average0;
        int32_t average1;
        int32_t average2;
        int32_t average3;
        int32_t composite;
        int32_t aid_score;
        int32_t support_score;
        uint32_t profile_hash;
        int special_index;
        uint32_t active_count;
        uint32_t avg_composite;
        uint32_t avg_attendance;
        int32_t funding_pressure;
        int absent_pressure;
        int risk_score_raw;
        uint32_t risk_score;
        uint32_t risk_tier;
        uint32_t top_bucket0;
        uint32_t top_bucket1;
        uint32_t signature;

        attendance_present = (uint32_t)record->attendance_present + secondary->attendance_present +
            tertiary->attendance_present + quaternary->attendance_present;
        attendance_absent = (uint32_t)record->attendance_absent + secondary->attendance_absent +
            tertiary->attendance_absent + quaternary->attendance_absent;
        attendance_late = (uint32_t)record->attendance_late + secondary->attendance_late +
            tertiary->attendance_late + quaternary->attendance_late;
        attendance_total = attendance_present + attendance_absent + attendance_late;
        attendance_rate = (uint16_t)((attendance_present * 10000u) / (attendance_total > 0u ? attendance_total : 1u));

        scholarship_amount = record->scholarship_amount + secondary->scholarship_amount +
            tertiary->scholarship_amount + quaternary->scholarship_amount;
        outstanding_balance = record->outstanding_balance + secondary->outstanding_balance +
            tertiary->outstanding_balance + quaternary->outstanding_balance;

        average0 = weighted_average(record->maths) + weighted_average(secondary->maths);
        average1 = weighted_average(record->reading) + weighted_average(tertiary->reading);
        average2 = weighted_average(record->science) + weighted_average(quaternary->science);
        average3 = weighted_average(record->writing) + weighted_average(secondary->writing);
        composite = (average0 * 35 + average1 * 25 + average2 * 20 + average3 * 20) / 100;
        aid_score = (int32_t)record->scholarship_amount - (int32_t)(record->outstanding_balance / 2u);
        support_score =
            ((record->support_bits & 0x01u) ? 1 : 0) +
            ((secondary->support_bits & 0x02u) ? 1 : 0) +
            ((tertiary->support_bits & 0x04u) ? 1 : 0) +
            ((quaternary->support_bits & 0x08u) ? 1 : 0);

        profile_hash = mix32(
            (record->student_id ^ secondary->student_id ^ tertiary->student_id ^ quaternary->student_id) ^
            ((uint32_t)composite << 1) ^
            ((uint32_t)attendance_rate << 2) ^
            ((uint32_t)(aid_score + (int32_t)secondary->scholarship_amount - (int32_t)(secondary->outstanding_balance / 2u)) << 3) ^
            ((uint32_t)record->grade_level << 7) ^
            ((uint32_t)(record->program_track ^ secondary->program_track ^ tertiary->program_track ^ quaternary->program_track) << 11) ^
            ((uint32_t)(record->cohort ^ secondary->cohort ^ tertiary->cohort ^ quaternary->cohort) << 13));

        aggregate = &context->aggregates[record->school->school_id - context->base_school_id];
        if (!aggregate->used)
        {
            aggregate->used = true;
            aggregate->school = record->school;
            aggregate->special_index = school_special_index(record->school);
        }

        special_index = aggregate->special_index;
        aggregate->active_count += 1u;
        aggregate->scholarship_count += scholarship_amount > 0u ? 1u : 0u;
        aggregate->support_count += support_score >= 2 ? 1u : 0u;
        aggregate->composite_sum += (uint64_t)composite;
        aggregate->attendance_sum += (uint64_t)attendance_rate;
        aggregate->funding_sum += (int64_t)outstanding_balance - (int64_t)(scholarship_amount / 2u) + special_index;
        aggregate->compliance_mix = mix32(aggregate->compliance_mix ^ profile_hash ^ (uint32_t)special_index);

        aggregate->tag_hist[record->tags[0] & 31u] += 1u;
        aggregate->tag_hist[record->tags[1] & 31u] += 1u;
        aggregate->tag_hist[record->tags[2] & 31u] += 1u;
        aggregate->tag_hist[record->tags[3] & 31u] += 1u;
        aggregate->tag_hist[secondary->tags[0] & 31u] += 1u;
        aggregate->tag_hist[secondary->tags[1] & 31u] += 1u;
        aggregate->tag_hist[tertiary->tags[0] & 31u] += 1u;
        aggregate->tag_hist[tertiary->tags[1] & 31u] += 1u;
        aggregate->tag_hist[quaternary->tags[0] & 31u] += 1u;
        aggregate->tag_hist[quaternary->tags[1] & 31u] += 1u;
        aggregate->tag_hist[(record->school->region_code + secondary->school->region_code +
            tertiary->school->region_code + quaternary->school->region_code) & 31u] += 1u;
        aggregate->tag_hist[(record->support_bits ^ secondary->program_track ^
            tertiary->cohort ^ quaternary->grade_level) & 31u] += 1u;

        active_count = aggregate->active_count;
        avg_composite = (uint32_t)(aggregate->composite_sum / active_count);
        avg_attendance = (uint32_t)(aggregate->attendance_sum / active_count);
        funding_pressure = (int32_t)floor_div_i64(aggregate->funding_sum, (int64_t)active_count);
        absent_pressure = (int)attendance_absent * 37 + (int)attendance_late * 19;
        risk_score_raw = 15000 - (int)avg_composite * 2 + (int)floor_div_i64((int64_t)funding_pressure, 4) +
            (int)aggregate->support_count * 13 + absent_pressure + special_index * 5 +
            (aggregate->compliance_mix & 127u);
        risk_score = (uint32_t)(risk_score_raw > 0 ? risk_score_raw : 0);
        risk_tier = risk_score >= 9000u ? 2u : risk_score >= 7000u ? 1u : 0u;

        if (active_count < 2u || (risk_tier == 0u && funding_pressure <= 100))
        {
            continue;
        }

        choose_top_two_buckets_manual(aggregate->tag_hist, &top_bucket0, &top_bucket1);
        signature = mix32(
            aggregate->school->school_id ^
            (risk_tier << 6) ^
            (active_count << 9) ^
            (avg_composite << 1) ^
            (avg_attendance << 3) ^
            ((uint32_t)funding_pressure << 5) ^
            top_bucket0 ^
            (top_bucket1 << 8));

        if (has_last_signature && signature == last_signature)
        {
            continue;
        }
        last_signature = signature;
        has_last_signature = true;

        if (!started && active_count < 3u)
        {
            continue;
        }
        started = true;

        manual_result.emitted_snapshots += 1u;
        manual_result.total_enrollment += active_count;
        manual_result.total_risk += risk_score;
        if (risk_tier > manual_result.max_risk_tier)
        {
            manual_result.max_risk_tier = risk_tier;
        }
        manual_result.last_school_id = aggregate->school->school_id;
        manual_result.digest = mix32(
            manual_result.digest ^
            mix32(aggregate->school->school_id) ^
            mix32(active_count) ^
            mix32(risk_score) ^
            mix32((uint32_t)funding_pressure) ^
            mix32(risk_tier));
    }

    return &manual_result;
}

#ifndef EDU_COMPLEX_MANUAL_HELPERS_ONLY
int main(int argc, char **argv)
{
    long long total_ns = 0;
    int runs = 3;
    ManualDataset *dataset;
    ManualContext *context;

    if (argc < 2)
    {
        fprintf(stderr, "usage: edu_complex_benchmark_manual <data.bin> [runs]\n");
        return 1;
    }

    dataset = load_manual_dataset(argv[1]);
    runs = argc > 2 ? atoi(argv[2]) : 3;
    if (dataset == NULL || dataset->records == NULL)
    {
        return 1;
    }

    context = build_manual_context(dataset->records);
    if (context == NULL)
    {
        fprintf(stderr, "failed to build manual context\n");
        free_manual_dataset(dataset);
        return 1;
    }

    for (int run = 0; run < runs; ++run)
    {
        struct timespec start;
        struct timespec end;
        reset_manual_context(context);
        clock_gettime(CLOCK_MONOTONIC, &start);
        g_result = run_manual_context(context, dataset->records);
        clock_gettime(CLOCK_MONOTONIC, &end);
        total_ns += (long long)(end.tv_sec - start.tv_sec) * 1000000000LL +
            (long long)(end.tv_nsec - start.tv_nsec);
    }

    print_result(g_result, (double)total_ns / runs / 1e6, runs);
    free_manual_context(context);
    free_manual_dataset(dataset);
    return 0;
}
#endif
