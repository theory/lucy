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

#define C_LUCY_INVERTER
#define C_LUCY_INVERTERENTRY
#include "XSBind.h"
#include "Lucy/Index/Inverter.h"
#include "Lucy/Document/Doc.h"
#include "Lucy/Index/Segment.h"
#include "Clownfish/ByteBuf.h"
#include "Lucy/Plan/FieldType.h"
#include "Lucy/Plan/BlobType.h"
#include "Lucy/Plan/NumericType.h"
#include "Lucy/Plan/Schema.h"
#include "Lucy/Plan/TextType.h"
#include "Clownfish/Util/StringHelper.h"

static lucy_InverterEntry*
S_fetch_entry(lucy_Inverter *self, HE *hash_entry) {
    lucy_InverterIVARS *const ivars = lucy_Inverter_IVARS(self);
    lucy_Schema *const schema = ivars->schema;
    char *key;
    STRLEN key_len;
    STRLEN he_key_len = HeKLEN(hash_entry);

    // Force field name to UTF-8 if necessary.
    if (he_key_len == (STRLEN)HEf_SVKEY) {
        SV *key_sv = HeKEY_sv(hash_entry);
        key = SvPVutf8(key_sv, key_len);
    }
    else {
        key = HeKEY(hash_entry);
        key_len = he_key_len;
        if (!cfish_StrHelp_utf8_valid(key, key_len)) {
            SV *key_sv = HeSVKEY_force(hash_entry);
            key = SvPVutf8(key_sv, key_len);
        }
    }

    cfish_ZombieCharBuf *field = CFISH_ZCB_WRAP_STR(key, key_len);
    int32_t field_num
        = Lucy_Seg_Field_Num(ivars->segment, (cfish_CharBuf*)field);
    if (!field_num) {
        // This field seems not to be in the segment yet.  Try to find it in
        // the Schema.
        if (Lucy_Schema_Fetch_Type(schema, (cfish_CharBuf*)field)) {
            // The field is in the Schema.  Get a field num from the Segment.
            field_num = Lucy_Seg_Add_Field(ivars->segment,
                                           (cfish_CharBuf*)field);
        }
        else {
            // We've truly failed to find the field.  The user must
            // not have spec'd it.
            THROW(CFISH_ERR, "Unknown field name: '%s'", key);
        }
    }

    lucy_InverterEntry *entry
        = (lucy_InverterEntry*)Cfish_VA_Fetch(ivars->entry_pool, field_num);
    if (!entry) {
        entry = lucy_InvEntry_new(schema, (cfish_CharBuf*)field, field_num);
        Cfish_VA_Store(ivars->entry_pool, field_num, (cfish_Obj*)entry);
    }
    return entry;
}

void
lucy_Inverter_invert_doc(lucy_Inverter *self, lucy_Doc *doc) {
    HV  *const fields = (HV*)Lucy_Doc_Get_Fields(doc);
    I32  num_keys     = hv_iterinit(fields);

    // Prepare for the new doc.
    Lucy_Inverter_Set_Doc(self, doc);

    // Extract and invert the doc's fields.
    while (num_keys--) {
        HE *hash_entry = hv_iternext(fields);
        lucy_InverterEntry *inv_entry = S_fetch_entry(self, hash_entry);
        SV *value_sv = HeVAL(hash_entry);
        lucy_InverterEntryIVARS *const entry_ivars
            = lucy_InvEntry_IVARS(inv_entry);
        lucy_FieldType *type = entry_ivars->type;

        // Get the field value, forcing text fields to UTF-8.
        switch (Lucy_FType_Primitive_ID(type) & lucy_FType_PRIMITIVE_ID_MASK) {
            case lucy_FType_TEXT: {
                    STRLEN val_len;
                    char *val_ptr = SvPVutf8(value_sv, val_len);
                    cfish_ViewCharBuf *value
                        = (cfish_ViewCharBuf*)entry_ivars->value;
                    Cfish_ViewCB_Assign_Str(value, val_ptr, val_len);
                    break;
                }
            case lucy_FType_BLOB: {
                    STRLEN val_len;
                    char *val_ptr = SvPV(value_sv, val_len);
                    cfish_ViewByteBuf *value
                        = (cfish_ViewByteBuf*)entry_ivars->value;
                    Cfish_ViewBB_Assign_Bytes(value, val_ptr, val_len);
                    break;
                }
            case lucy_FType_INT32: {
                    cfish_Integer32* value = (cfish_Integer32*)entry_ivars->value;
                    Cfish_Int32_Set_Value(value, SvIV(value_sv));
                    break;
                }
            case lucy_FType_INT64: {
                    cfish_Integer64* value = (cfish_Integer64*)entry_ivars->value;
                    int64_t val = sizeof(IV) == 8
                                  ? SvIV(value_sv)
                                  : (int64_t)SvNV(value_sv); // lossy
                    Cfish_Int64_Set_Value(value, val);
                    break;
                }
            case lucy_FType_FLOAT32: {
                    cfish_Float32* value = (cfish_Float32*)entry_ivars->value;
                    Cfish_Float32_Set_Value(value, (float)SvNV(value_sv));
                    break;
                }
            case lucy_FType_FLOAT64: {
                    cfish_Float64* value = (cfish_Float64*)entry_ivars->value;
                    Cfish_Float64_Set_Value(value, SvNV(value_sv));
                    break;
                }
            default:
                THROW(CFISH_ERR, "Unrecognized type: %o", type);
        }

        Lucy_Inverter_Add_Field(self, inv_entry);
    }
}


