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

#define C_LUCY_NORMALIZER
#define C_LUCY_TOKEN
#include <ctype.h>
#include "Lucy/Util/ToolSet.h"

#include "Lucy/Analysis/Normalizer.h"
#include "Lucy/Analysis/Token.h"
#include "Lucy/Analysis/Inversion.h"

#include "utf8proc.h"

#define INITIAL_BUFSIZE 63

Normalizer*
Normalizer_new(const CharBuf *form, bool case_fold, bool strip_accents) {
    Normalizer *self = (Normalizer*)VTable_Make_Obj(NORMALIZER);
    return Normalizer_init(self, form, case_fold, strip_accents);
}

Normalizer*
Normalizer_init(Normalizer *self, const CharBuf *form, bool case_fold,
                bool strip_accents) {
    int options = UTF8PROC_STABLE;
    NormalizerIVARS *const ivars = Normalizer_IVARS(self);

    if (form == NULL
        || CB_Equals_Str(form, "NFKC", 4) || CB_Equals_Str(form, "nfkc", 4)
       ) {
        options |= UTF8PROC_COMPOSE | UTF8PROC_COMPAT;
    }
    else if (CB_Equals_Str(form, "NFC", 3) || CB_Equals_Str(form, "nfc", 3)) {
        options |= UTF8PROC_COMPOSE;
    }
    else if (CB_Equals_Str(form, "NFKD", 4) || CB_Equals_Str(form, "nfkd", 4)) {
        options |= UTF8PROC_DECOMPOSE | UTF8PROC_COMPAT;
    }
    else if (CB_Equals_Str(form, "NFD", 3) || CB_Equals_Str(form, "nfd", 3)) {
        options |= UTF8PROC_DECOMPOSE;
    }
    else {
        THROW(ERR, "Invalid normalization form %o", form);
    }

    if (case_fold)     { options |= UTF8PROC_CASEFOLD; }
    if (strip_accents) { options |= UTF8PROC_STRIPMARK; }

    ivars->options = options;

    return self;
}

Inversion*
Normalizer_transform(Normalizer *self, Inversion *inversion) {
    // allocate additional space because utf8proc_reencode adds a
    // terminating null char
    int32_t static_buffer[INITIAL_BUFSIZE + 1];
    int32_t *buffer = static_buffer;
    ssize_t bufsize = INITIAL_BUFSIZE;
    Token *token;
    NormalizerIVARS *const ivars = Normalizer_IVARS(self);

    while (NULL != (token = Inversion_Next(inversion))) {
        TokenIVARS *const token_ivars = Token_IVARS(token);
        ssize_t len
            = utf8proc_decompose((uint8_t*)token_ivars->text,
                                 token_ivars->len, buffer, bufsize,
                                 ivars->options);

        if (len > bufsize) {
            // buffer too small, (re)allocate
            if (buffer != static_buffer) {
                FREEMEM(buffer);
            }
            // allocate additional INITIAL_BUFSIZE items
            bufsize = len + INITIAL_BUFSIZE;
            buffer = (int32_t*)MALLOCATE((bufsize + 1) * sizeof(int32_t));
            len = utf8proc_decompose((uint8_t*)token_ivars->text,
                                     token_ivars->len, buffer, bufsize,
                                     ivars->options);
        }
        if (len < 0) {
            continue;
        }

        len = utf8proc_reencode(buffer, len, ivars->options);

        if (len >= 0) {
            if (len > (ssize_t)token_ivars->len) {
                FREEMEM(token_ivars->text);
                token_ivars->text = (char*)MALLOCATE(len + 1);
            }
            memcpy(token_ivars->text, buffer, len + 1);
            token_ivars->len = len;
        }
    }

    if (buffer != static_buffer) {
        FREEMEM(buffer);
    }

    Inversion_Reset(inversion);
    return (Inversion*)INCREF(inversion);
}

Hash*
Normalizer_dump(Normalizer *self) {
    Normalizer_Dump_t super_dump
        = SUPER_METHOD_PTR(NORMALIZER, Lucy_Normalizer_Dump);
    Hash *dump = super_dump(self);
    int options = Normalizer_IVARS(self)->options;

    CharBuf *form = options & UTF8PROC_COMPOSE ?
                    options & UTF8PROC_COMPAT ?
                    CB_new_from_trusted_utf8("NFKC", 4) :
                    CB_new_from_trusted_utf8("NFC", 3) :
                        options & UTF8PROC_COMPAT ?
                        CB_new_from_trusted_utf8("NFKD", 4) :
                        CB_new_from_trusted_utf8("NFD", 3);

    Hash_Store_Str(dump, "normalization_form", 18, (Obj*)form);

    BoolNum *case_fold = Bool_singleton(options & UTF8PROC_CASEFOLD);
    Hash_Store_Str(dump, "case_fold", 9, (Obj*)case_fold);

    BoolNum *strip_accents = Bool_singleton(options & UTF8PROC_STRIPMARK);
    Hash_Store_Str(dump, "strip_accents", 13, (Obj*)strip_accents);

    return dump;
}

Normalizer*
Normalizer_load(Normalizer *self, Obj *dump) {
    Normalizer_Load_t super_load
        = SUPER_METHOD_PTR(NORMALIZER, Lucy_Normalizer_Load);
    Normalizer *loaded = super_load(self, dump);
    Hash    *source = (Hash*)CERTIFY(dump, HASH);

    Obj *obj = Hash_Fetch_Str(source, "normalization_form", 18);
    CharBuf *form = (CharBuf*)CERTIFY(obj, CHARBUF);
    obj = Hash_Fetch_Str(source, "case_fold", 9);
    bool case_fold = Bool_Get_Value((BoolNum*)CERTIFY(obj, BOOLNUM));
    obj = Hash_Fetch_Str(source, "strip_accents", 13);
    bool strip_accents = Bool_Get_Value((BoolNum*)CERTIFY(obj, BOOLNUM));

    return Normalizer_init(loaded, form, case_fold, strip_accents);
}

bool
Normalizer_equals(Normalizer *self, Obj *other) {
    if ((Normalizer*)other == self)       { return true; }
    if (!Obj_Is_A(other, NORMALIZER))     { return false; }
    NormalizerIVARS *const ivars = Normalizer_IVARS(self);
    NormalizerIVARS *const ovars = Normalizer_IVARS((Normalizer*)other);
    if (ovars->options != ivars->options) { return false; }
    return true;
}


