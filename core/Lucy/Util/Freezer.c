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

#define C_LUCY_FREEZER
#include "Lucy/Util/ToolSet.h"

#include "Lucy/Util/Freezer.h"
#include "Lucy/Store/InStream.h"
#include "Lucy/Store/OutStream.h"
#include "Lucy/Document/Doc.h"
#include "Lucy/Index/Similarity.h"
#include "Lucy/Index/DocVector.h"
#include "Lucy/Index/TermVector.h"
#include "Lucy/Search/Query.h"
#include "Lucy/Search/SortRule.h"
#include "Lucy/Search/SortSpec.h"
#include "Lucy/Search/MatchDoc.h"
#include "Lucy/Search/TopDocs.h"

void
Freezer_freeze(Obj *obj, OutStream *outstream) {
    Freezer_serialize_charbuf(Obj_Get_Class_Name(obj), outstream);
    Freezer_serialize(obj, outstream);
}

Obj*
Freezer_thaw(InStream *instream) {
    CharBuf *class_name = Freezer_read_charbuf(instream);
    VTable *vtable = VTable_singleton(class_name, NULL);
    Obj *blank = VTable_Make_Obj(vtable);
    DECREF(class_name);
    return Freezer_deserialize(blank, instream);
}

void
Freezer_serialize(Obj *obj, OutStream *outstream) {
    VTable *vtable = Obj_Get_VTable(obj);
    if (Obj_Is_A(obj, CHARBUF)) {
        Freezer_serialize_charbuf((CharBuf*)obj, outstream);
    }
    else if (Obj_Is_A(obj, BYTEBUF)) {
        Freezer_serialize_bytebuf((ByteBuf*)obj, outstream);
    }
    else if (Obj_Is_A(obj, VARRAY)) {
        Freezer_serialize_varray((VArray*)obj, outstream);
    }
    else if (Obj_Is_A(obj, HASH)) {
        Freezer_serialize_hash((Hash*)obj, outstream);
    }
    else if (Obj_Is_A(obj, NUM)) {
        if (Obj_Is_A(obj, INTNUM)) {
            if (Obj_Is_A(obj, BOOLNUM)) {
                bool val = Bool_Get_Value((BoolNum*)obj);
                OutStream_Write_U8(outstream, (uint8_t)val);
            }
            else if (Obj_Is_A(obj, INTEGER32)) {
                int32_t val = Int32_Get_Value((Integer32*)obj);
                OutStream_Write_C32(outstream, (uint32_t)val);
            }
            else if (Obj_Is_A(obj, INTEGER64)) {
                int64_t val = Int64_Get_Value((Integer64*)obj);
                OutStream_Write_C64(outstream, (uint64_t)val);
            }
        }
        else if (Obj_Is_A(obj, FLOATNUM)) {
            if (Obj_Is_A(obj, FLOAT32)) {
                float val = Float32_Get_Value((Float32*)obj);
                OutStream_Write_F32(outstream, val);
            }
            else if (Obj_Is_A(obj, FLOAT64)) {
                double val = Float64_Get_Value((Float64*)obj);
                OutStream_Write_F64(outstream, val);
            }
        }
    }
    else if (Obj_Is_A(obj, QUERY)) {
        Query_Serialize((Query*)obj, outstream);
    }
    else if (Obj_Is_A(obj, DOC)) {
        Doc_Serialize((Doc*)obj, outstream);
    }
    else if (Obj_Is_A(obj, DOCVECTOR)) {
        DocVec_Serialize((DocVector*)obj, outstream);
    }
    else if (Obj_Is_A(obj, TERMVECTOR)) {
        TV_Serialize((TermVector*)obj, outstream);
    }
    else if (Obj_Is_A(obj, SIMILARITY)) {
        Sim_Serialize((Similarity*)obj, outstream);
    }
    else if (Obj_Is_A(obj, MATCHDOC)) {
        MatchDoc_Serialize((MatchDoc*)obj, outstream);
    }
    else if (Obj_Is_A(obj, TOPDOCS)) {
        TopDocs_Serialize((TopDocs*)obj, outstream);
    }
    else if (Obj_Is_A(obj, SORTSPEC)) {
        SortSpec_Serialize((SortSpec*)obj, outstream);
    }
    else if (Obj_Is_A(obj, SORTRULE)) {
        SortRule_Serialize((SortRule*)obj, outstream);
    }
    else {
        THROW(ERR, "Don't know how to serialize a %o",
              Obj_Get_Class_Name(obj));
    }
}

Obj*
Freezer_deserialize(Obj *obj, InStream *instream) {
    VTable *vtable = Obj_Get_VTable(obj);
    if (Obj_Is_A(obj, CHARBUF)) {
        obj = (Obj*)Freezer_deserialize_charbuf((CharBuf*)obj, instream);
    }
    else if (Obj_Is_A(obj, BYTEBUF)) {
        obj = (Obj*)Freezer_deserialize_bytebuf((ByteBuf*)obj, instream);
    }
    else if (Obj_Is_A(obj, VARRAY)) {
        obj = (Obj*)Freezer_deserialize_varray((VArray*)obj, instream);
    }
    else if (Obj_Is_A(obj, HASH)) {
        obj = (Obj*)Freezer_deserialize_hash((Hash*)obj, instream);
    }
    else if (Obj_Is_A(obj, NUM)) {
        if (Obj_Is_A(obj, INTNUM)) {
            if (Obj_Is_A(obj, BOOLNUM)) {
                bool value = (bool)InStream_Read_U8(instream);
                BoolNum *self = (BoolNum*)obj;
                if (self && self != CFISH_TRUE && self != CFISH_FALSE) {
                    Bool_Dec_RefCount_t super_decref
                        = SUPER_METHOD_PTR(BOOLNUM, Cfish_Bool_Dec_RefCount);
                    super_decref(self);
                }
                obj = value ? CFISH_TRUE : CFISH_FALSE;
            }
            else if (Obj_Is_A(obj, INTEGER32)) {
                int32_t value = (int32_t)InStream_Read_C32(instream);
                obj = (Obj*)Int32_init((Integer32*)obj, value);
            }
            else if (Obj_Is_A(obj, INTEGER64)) {
                int64_t value = (int64_t)InStream_Read_C64(instream);
                obj = (Obj*)Int64_init((Integer64*)obj, value);
            }
        }
        else if (Obj_Is_A(obj, FLOATNUM)) {
            if (Obj_Is_A(obj, FLOAT32)) {
                float value = InStream_Read_F32(instream);
                obj = (Obj*)Float32_init((Float32*)obj, value);
            }
            else if (Obj_Is_A(obj, FLOAT64)) {
                double value = InStream_Read_F64(instream);
                obj = (Obj*)Float64_init((Float64*)obj, value);
            }
        }
    }
    else if (Obj_Is_A(obj, QUERY)) {
        obj = (Obj*)Query_Deserialize((Query*)obj, instream);
    }
    else if (Obj_Is_A(obj, DOC)) {
        obj = (Obj*)Doc_Deserialize((Doc*)obj, instream);
    }
    else if (Obj_Is_A(obj, DOCVECTOR)) {
        obj = (Obj*)DocVec_Deserialize((DocVector*)obj, instream);
    }
    else if (Obj_Is_A(obj, TERMVECTOR)) {
        obj = (Obj*)TV_Deserialize((TermVector*)obj, instream);
    }
    else if (Obj_Is_A(obj, SIMILARITY)) {
        obj = (Obj*)Sim_Deserialize((Similarity*)obj, instream);
    }
    else if (Obj_Is_A(obj, MATCHDOC)) {
        obj = (Obj*)MatchDoc_Deserialize((MatchDoc*)obj, instream);
    }
    else if (Obj_Is_A(obj, TOPDOCS)) {
        obj = (Obj*)TopDocs_Deserialize((TopDocs*)obj, instream);
    }
    else if (Obj_Is_A(obj, SORTSPEC)) {
        obj = (Obj*)SortSpec_Deserialize((SortSpec*)obj, instream);
    }
    else if (Obj_Is_A(obj, SORTRULE)) {
        obj = (Obj*)SortRule_Deserialize((SortRule*)obj, instream);
    }
    else {
        THROW(ERR, "Don't know how to deserialize a %o",
              Obj_Get_Class_Name(obj));
    }

    return obj;
}

void
Freezer_serialize_charbuf(CharBuf *charbuf, OutStream *outstream) {
    size_t size  = CB_Get_Size(charbuf);
    uint8_t *buf = CB_Get_Ptr8(charbuf);
    OutStream_Write_C64(outstream, size);
    OutStream_Write_Bytes(outstream, buf, size);
}

CharBuf*
Freezer_deserialize_charbuf(CharBuf *charbuf, InStream *instream) {
    size_t size = InStream_Read_C32(instream);
    if (size == SIZE_MAX) {
        THROW(ERR, "Can't deserialize SIZE_MAX bytes");
    }
    size_t cap = Memory_oversize(size + 1, sizeof(char));
    char *buf = MALLOCATE(cap);
    InStream_Read_Bytes(instream, buf, size);
    buf[size] = '\0';
    if (!StrHelp_utf8_valid(buf, size)) {
        THROW(ERR, "Attempt to deserialize invalid UTF-8");
    }
    return CB_init_steal_trusted_str(charbuf, buf, size, cap);
}

CharBuf*
Freezer_read_charbuf(InStream *instream) {
    CharBuf *charbuf = (CharBuf*)VTable_Make_Obj(CHARBUF);
    return Freezer_deserialize_charbuf(charbuf, instream);
}

void
Freezer_serialize_bytebuf(ByteBuf *bytebuf, OutStream *outstream) {
    size_t size = BB_Get_Size(bytebuf);
    OutStream_Write_C32(outstream, size);
    OutStream_Write_Bytes(outstream, BB_Get_Buf(bytebuf), size);
}

ByteBuf*
Freezer_deserialize_bytebuf(ByteBuf *bytebuf, InStream *instream) {
    size_t size = InStream_Read_C32(instream);
    char   *buf = MALLOCATE(size);
    InStream_Read_Bytes(instream, buf, size);
    return BB_init_steal_bytes(bytebuf, buf, size, size);
}

ByteBuf*
Freezer_read_bytebuf(InStream *instream) {
    ByteBuf *bytebuf = (ByteBuf*)VTable_Make_Obj(BYTEBUF);
    return Freezer_deserialize_bytebuf(bytebuf, instream);
}

void
Freezer_serialize_varray(VArray *array, OutStream *outstream) {
    uint32_t last_valid_tick = 0;
    size_t size = VA_Get_Size(array);
    OutStream_Write_C32(outstream, size);
    for (uint32_t i = 0; i < size; i++) {
        Obj *elem = VA_Fetch(array, i);
        if (elem) {
            OutStream_Write_C32(outstream, i - last_valid_tick);
            FREEZE(elem, outstream);
            last_valid_tick = i;
        }
    }
    // Terminate.
    OutStream_Write_C32(outstream, size - last_valid_tick);
}

VArray*
Freezer_deserialize_varray(VArray *array, InStream *instream) {
    uint32_t size = InStream_Read_C32(instream);
    VA_init(array, size);
    for (uint32_t tick = InStream_Read_C32(instream);
         tick < size;
         tick += InStream_Read_C32(instream)
        ) {
        Obj *obj = THAW(instream);
        VA_Store(array, tick, obj);
    }
    VA_Resize(array, size);
    return array;
}

VArray*
Freezer_read_varray(InStream *instream) {
    VArray *array = (VArray*)VTable_Make_Obj(VARRAY);
    return Freezer_deserialize_varray(array, instream);
}

void
Freezer_serialize_hash(Hash *hash, OutStream *outstream) {
    Obj *key;
    Obj *val;
    uint32_t charbuf_count = 0;
    uint32_t hash_size = Hash_Get_Size(hash);
    OutStream_Write_C32(outstream, hash_size);

    // Write CharBuf keys first.  CharBuf keys are the common case; grouping
    // them together is a form of run-length-encoding and saves space, since
    // we omit the per-key class name.
    Hash_Iterate(hash);
    while (Hash_Next(hash, &key, &val)) {
        if (Obj_Is_A(key, CHARBUF)) { charbuf_count++; }
    }
    OutStream_Write_C32(outstream, charbuf_count);
    Hash_Iterate(hash);
    while (Hash_Next(hash, &key, &val)) {
        if (Obj_Is_A(key, CHARBUF)) {
            Freezer_serialize_charbuf((CharBuf*)key, outstream);
            FREEZE(val, outstream);
        }
    }

    // Punt on the classes of the remaining keys.
    Hash_Iterate(hash);
    while (Hash_Next(hash, &key, &val)) {
        if (!Obj_Is_A(key, CHARBUF)) {
            FREEZE(key, outstream);
            FREEZE(val, outstream);
        }
    }
}

Hash*
Freezer_deserialize_hash(Hash *hash, InStream *instream) {
    uint32_t size         = InStream_Read_C32(instream);
    uint32_t num_charbufs = InStream_Read_C32(instream);
    uint32_t num_other    = size - num_charbufs;
    CharBuf *key          = num_charbufs ? CB_new(0) : NULL;

    Hash_init(hash, size);

    // Read key-value pairs with CharBuf keys.
    while (num_charbufs--) {
        uint32_t len = InStream_Read_C32(instream);
        char *key_buf = CB_Grow(key, len);
        InStream_Read_Bytes(instream, key_buf, len);
        key_buf[len] = '\0';
        CB_Set_Size(key, len);
        Hash_Store(hash, (Obj*)key, THAW(instream));
    }
    DECREF(key);

    // Read remaining key/value pairs.
    while (num_other--) {
        Obj *k = THAW(instream);
        Hash_Store(hash, k, THAW(instream));
        DECREF(k);
    }

    return hash;
}

Hash*
Freezer_read_hash(InStream *instream) {
    Hash *hash = (Hash*)VTable_Make_Obj(HASH);
    return Freezer_deserialize_hash(hash, instream);
}

