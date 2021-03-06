/* Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <ctype.h>

#define C_LUCY_SEGMENT
#include "Lucy/Util/ToolSet.h"

#include "Lucy/Index/Segment.h"
#include "Lucy/Store/Folder.h"
#include "Lucy/Util/Json.h"
#include "Clownfish/Util/StringHelper.h"
#include "Lucy/Util/IndexFileNames.h"

Segment*
Seg_new(int64_t number) {
    Segment *self = (Segment*)VTable_Make_Obj(SEGMENT);
    return Seg_init(self, number);
}

Segment*
Seg_init(Segment *self, int64_t number) {
    SegmentIVARS *const ivars = Seg_IVARS(self);

    // Validate.
    if (number < 0) { THROW(ERR, "Segment number %i64 less than 0", number); }

    // Init.
    ivars->metadata  = Hash_new(0);
    ivars->count     = 0;
    ivars->by_num    = VA_new(2);
    ivars->by_name   = Hash_new(0);

    // Start field numbers at 1, not 0.
    VA_Push(ivars->by_num, (Obj*)CB_newf(""));

    // Assign.
    ivars->number = number;

    // Derive.
    ivars->name = Seg_num_to_name(number);

    return self;
}

CharBuf*
Seg_num_to_name(int64_t number) {
    char base36[StrHelp_MAX_BASE36_BYTES];
    StrHelp_to_base36(number, &base36);
    return CB_newf("seg_%s", &base36);
}

bool
Seg_valid_seg_name(const CharBuf *name) {
    if (CB_Starts_With_Str(name, "seg_", 4)) {
        ZombieCharBuf *scratch = ZCB_WRAP(name);
        ZCB_Nip(scratch, 4);
        uint32_t code_point;
        while (0 != (code_point = ZCB_Nip_One(scratch))) {
            if (!isalnum(code_point)) { return false; }
        }
        if (ZCB_Get_Size(scratch) == 0) { return true; } // Success!
    }
    return false;
}

void
Seg_destroy(Segment *self) {
    SegmentIVARS *const ivars = Seg_IVARS(self);
    DECREF(ivars->name);
    DECREF(ivars->metadata);
    DECREF(ivars->by_name);
    DECREF(ivars->by_num);
    SUPER_DESTROY(self, SEGMENT);
}

bool
Seg_read_file(Segment *self, Folder *folder) {
    SegmentIVARS *const ivars = Seg_IVARS(self);
    CharBuf *filename = CB_newf("%o/segmeta.json", ivars->name);
    Hash    *metadata = (Hash*)Json_slurp_json(folder, filename);
    Hash    *my_metadata;

    // Bail unless the segmeta file was read successfully.
    DECREF(filename);
    if (!metadata) { return false; }
    CERTIFY(metadata, HASH);

    // Grab metadata for the Segment object itself.
    DECREF(ivars->metadata);
    ivars->metadata = metadata;
    my_metadata
        = (Hash*)CERTIFY(Hash_Fetch_Str(ivars->metadata, "segmeta", 7), HASH);

    // Assign.
    Obj *count = Hash_Fetch_Str(my_metadata, "count", 5);
    if (!count) { count = Hash_Fetch_Str(my_metadata, "doc_count", 9); }
    if (!count) { THROW(ERR, "Missing 'count'"); }
    else { ivars->count = Obj_To_I64(count); }

    // Get list of field nums.
    VArray *source_by_num = (VArray*)Hash_Fetch_Str(my_metadata,
                                                    "field_names", 11);
    uint32_t num_fields = source_by_num ? VA_Get_Size(source_by_num) : 0;
    if (source_by_num == NULL) {
        THROW(ERR, "Failed to extract 'field_names' from metadata");
    }

    // Init.
    DECREF(ivars->by_num);
    DECREF(ivars->by_name);
    ivars->by_num  = VA_new(num_fields);
    ivars->by_name = Hash_new(num_fields);

    // Copy the list of fields from the source.
    for (uint32_t i = 0; i < num_fields; i++) {
        CharBuf *name = (CharBuf*)VA_Fetch(source_by_num, i);
        Seg_Add_Field(self, name);
    }

    return true;
}

void
Seg_write_file(Segment *self, Folder *folder) {
    SegmentIVARS *const ivars = Seg_IVARS(self);
    Hash *my_metadata = Hash_new(16);

    // Store metadata specific to this Segment object.
    Hash_Store_Str(my_metadata, "count", 5,
                   (Obj*)CB_newf("%i64", ivars->count));
    Hash_Store_Str(my_metadata, "name", 4, (Obj*)CB_Clone(ivars->name));
    Hash_Store_Str(my_metadata, "field_names", 11, INCREF(ivars->by_num));
    Hash_Store_Str(my_metadata, "format", 6, (Obj*)CB_newf("%i32", 1));
    Hash_Store_Str(ivars->metadata, "segmeta", 7, (Obj*)my_metadata);

    CharBuf *filename = CB_newf("%o/segmeta.json", ivars->name);
    bool result = Json_spew_json((Obj*)ivars->metadata, folder, filename);
    DECREF(filename);
    if (!result) { RETHROW(INCREF(Err_get_error())); }
}

int32_t
Seg_add_field(Segment *self, const CharBuf *field) {
    SegmentIVARS *const ivars = Seg_IVARS(self);
    Integer32 *num = (Integer32*)Hash_Fetch(ivars->by_name, (Obj*)field);
    if (num) {
        return Int32_Get_Value(num);
    }
    else {
        int32_t field_num = VA_Get_Size(ivars->by_num);
        Hash_Store(ivars->by_name, (Obj*)field, (Obj*)Int32_new(field_num));
        VA_Push(ivars->by_num, (Obj*)CB_Clone(field));
        return field_num;
    }
}

CharBuf*
Seg_get_name(Segment *self) {
    return Seg_IVARS(self)->name;
}

int64_t
Seg_get_number(Segment *self) {
    return Seg_IVARS(self)->number;
}

void
Seg_set_count(Segment *self, int64_t count) {
    Seg_IVARS(self)->count = count;
}

int64_t
Seg_get_count(Segment *self) {
    return Seg_IVARS(self)->count;
}

int64_t
Seg_increment_count(Segment *self, int64_t increment) {
    SegmentIVARS *const ivars = Seg_IVARS(self);
    ivars->count += increment;
    return ivars->count;
}

void
Seg_store_metadata(Segment *self, const CharBuf *key, Obj *value) {
    SegmentIVARS *const ivars = Seg_IVARS(self);
    if (Hash_Fetch(ivars->metadata, (Obj*)key)) {
        THROW(ERR, "Metadata key '%o' already registered", key);
    }
    Hash_Store(ivars->metadata, (Obj*)key, value);
}

void
Seg_store_metadata_str(Segment *self, const char *key, size_t key_len,
                       Obj *value) {
    ZombieCharBuf *k = ZCB_WRAP_STR((char*)key, key_len);
    Seg_Store_Metadata(self, (CharBuf*)k, value);
}

Obj*
Seg_fetch_metadata(Segment *self, const CharBuf *key) {
    SegmentIVARS *const ivars = Seg_IVARS(self);
    return Hash_Fetch(ivars->metadata, (Obj*)key);
}

Obj*
Seg_fetch_metadata_str(Segment *self, const char *key, size_t len) {
    SegmentIVARS *const ivars = Seg_IVARS(self);
    return Hash_Fetch_Str(ivars->metadata, key, len);
}

Hash*
Seg_get_metadata(Segment *self) {
    return Seg_IVARS(self)->metadata;
}

int32_t
Seg_compare_to(Segment *self, Obj *other) {
    Segment *other_seg = (Segment*)CERTIFY(other, SEGMENT);
    SegmentIVARS *const ivars = Seg_IVARS(self);
    SegmentIVARS *const ovars = Seg_IVARS(other_seg);
    if (ivars->number < ovars->number)       { return -1; }
    else if (ivars->number == ovars->number) { return 0;  }
    else                                     { return 1;  }
}

CharBuf*
Seg_field_name(Segment *self, int32_t field_num) {
    SegmentIVARS *const ivars = Seg_IVARS(self);
    return field_num
           ? (CharBuf*)VA_Fetch(ivars->by_num, field_num)
           : NULL;
}

int32_t
Seg_field_num(Segment *self, const CharBuf *field) {
    if (field == NULL) {
        return 0;
    }
    else {
        SegmentIVARS *const ivars = Seg_IVARS(self);
        Integer32 *num = (Integer32*)Hash_Fetch(ivars->by_name, (Obj*)field);
        return num ? Int32_Get_Value(num) : 0;
    }
}


