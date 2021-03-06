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

#define C_LUCY_PHRASEQUERY
#define C_LUCY_PHRASECOMPILER

#include "Lucy/Util/ToolSet.h"

#include "Lucy/Search/PhraseQuery.h"
#include "Lucy/Index/DocVector.h"
#include "Lucy/Index/Posting.h"
#include "Lucy/Index/Posting/ScorePosting.h"
#include "Lucy/Index/PostingList.h"
#include "Lucy/Index/PostingListReader.h"
#include "Lucy/Index/SegPostingList.h"
#include "Lucy/Index/SegReader.h"
#include "Lucy/Index/Similarity.h"
#include "Lucy/Index/TermVector.h"
#include "Lucy/Plan/Schema.h"
#include "Lucy/Search/PhraseMatcher.h"
#include "Lucy/Search/Searcher.h"
#include "Lucy/Search/Span.h"
#include "Lucy/Search/TermQuery.h"
#include "Lucy/Store/InStream.h"
#include "Lucy/Store/OutStream.h"
#include "Lucy/Util/Freezer.h"

// Shared initialization routine which assumes that it's ok to assume control
// over [field] and [terms], eating their refcounts.
static PhraseQuery*
S_do_init(PhraseQuery *self, CharBuf *field, VArray *terms, float boost);

PhraseQuery*
PhraseQuery_new(const CharBuf *field, VArray *terms) {
    PhraseQuery *self = (PhraseQuery*)VTable_Make_Obj(PHRASEQUERY);
    return PhraseQuery_init(self, field, terms);
}

PhraseQuery*
PhraseQuery_init(PhraseQuery *self, const CharBuf *field, VArray *terms) {
    return S_do_init(self, CB_Clone(field), VA_Clone(terms), 1.0f);
}

void
PhraseQuery_destroy(PhraseQuery *self) {
    PhraseQueryIVARS *const ivars = PhraseQuery_IVARS(self);
    DECREF(ivars->terms);
    DECREF(ivars->field);
    SUPER_DESTROY(self, PHRASEQUERY);
}

static PhraseQuery*
S_do_init(PhraseQuery *self, CharBuf *field, VArray *terms, float boost) {
    Query_init((Query*)self, boost);
    PhraseQueryIVARS *const ivars = PhraseQuery_IVARS(self);
    for (uint32_t i = 0, max = VA_Get_Size(terms); i < max; i++) {
        CERTIFY(VA_Fetch(terms, i), OBJ);
    }
    ivars->field = field;
    ivars->terms = terms;
    return self;
}

void
PhraseQuery_serialize(PhraseQuery *self, OutStream *outstream) {
    PhraseQueryIVARS *const ivars = PhraseQuery_IVARS(self);
    OutStream_Write_F32(outstream, ivars->boost);
    Freezer_serialize_charbuf(ivars->field, outstream);
    Freezer_serialize_varray(ivars->terms, outstream);
}

PhraseQuery*
PhraseQuery_deserialize(PhraseQuery *self, InStream *instream) {
    float boost = InStream_Read_F32(instream);
    CharBuf *field = Freezer_read_charbuf(instream);
    VArray  *terms = Freezer_read_varray(instream);
    return S_do_init(self, field, terms, boost);
}

bool
PhraseQuery_equals(PhraseQuery *self, Obj *other) {
    if ((PhraseQuery*)other == self)   { return true; }
    if (!Obj_Is_A(other, PHRASEQUERY)) { return false; }
    PhraseQueryIVARS *const ivars = PhraseQuery_IVARS(self);
    PhraseQueryIVARS *const ovars = PhraseQuery_IVARS((PhraseQuery*)other);
    if (ivars->boost != ovars->boost)  { return false; }
    if (ivars->field && !ovars->field) { return false; }
    if (!ivars->field && ovars->field) { return false; }
    if (ivars->field && !CB_Equals(ivars->field, (Obj*)ovars->field)) {
        return false;
    }
    if (!VA_Equals(ovars->terms, (Obj*)ivars->terms)) { return false; }
    return true;
}

CharBuf*
PhraseQuery_to_string(PhraseQuery *self) {
    PhraseQueryIVARS *const ivars = PhraseQuery_IVARS(self);
    uint32_t  num_terms = VA_Get_Size(ivars->terms);
    CharBuf  *retval    = CB_Clone(ivars->field);
    CB_Cat_Trusted_Str(retval, ":\"", 2);
    for (uint32_t i = 0; i < num_terms; i++) {
        Obj     *term        = VA_Fetch(ivars->terms, i);
        CharBuf *term_string = Obj_To_String(term);
        CB_Cat(retval, term_string);
        DECREF(term_string);
        if (i < num_terms - 1) {
            CB_Cat_Trusted_Str(retval, " ",  1);
        }
    }
    CB_Cat_Trusted_Str(retval, "\"", 1);
    return retval;
}

Compiler*
PhraseQuery_make_compiler(PhraseQuery *self, Searcher *searcher,
                          float boost, bool subordinate) {
    PhraseQueryIVARS *const ivars = PhraseQuery_IVARS(self);
    if (VA_Get_Size(ivars->terms) == 1) {
        // Optimize for one-term "phrases".
        Obj *term = VA_Fetch(ivars->terms, 0);
        TermQuery *term_query = TermQuery_new(ivars->field, term);
        TermCompiler *term_compiler;
        TermQuery_Set_Boost(term_query, ivars->boost);
        term_compiler
            = (TermCompiler*)TermQuery_Make_Compiler(term_query, searcher,
                                                     boost, subordinate);
        DECREF(term_query);
        return (Compiler*)term_compiler;
    }
    else {
        PhraseCompiler *compiler
            = PhraseCompiler_new(self, searcher, boost);
        if (!subordinate) {
            PhraseCompiler_Normalize(compiler);
        }
        return (Compiler*)compiler;
    }
}

CharBuf*
PhraseQuery_get_field(PhraseQuery *self) {
    return PhraseQuery_IVARS(self)->field;
}

VArray*
PhraseQuery_get_terms(PhraseQuery *self) {
    return PhraseQuery_IVARS(self)->terms;
}

/*********************************************************************/

PhraseCompiler*
PhraseCompiler_new(PhraseQuery *parent, Searcher *searcher, float boost) {
    PhraseCompiler *self = (PhraseCompiler*)VTable_Make_Obj(PHRASECOMPILER);
    return PhraseCompiler_init(self, parent, searcher, boost);
}

PhraseCompiler*
PhraseCompiler_init(PhraseCompiler *self, PhraseQuery *parent,
                    Searcher *searcher, float boost) {
    PhraseCompilerIVARS *const ivars = PhraseCompiler_IVARS(self);
    PhraseQueryIVARS *const parent_ivars = PhraseQuery_IVARS(parent);
    Schema     *schema = Searcher_Get_Schema(searcher);
    Similarity *sim    = Schema_Fetch_Sim(schema, parent_ivars->field);
    VArray     *terms  = parent_ivars->terms;

    // Try harder to find a Similarity if necessary.
    if (!sim) { sim = Schema_Get_Similarity(schema); }

    // Init.
    Compiler_init((Compiler*)self, (Query*)parent, searcher, sim, boost);

    // Store IDF for the phrase.
    ivars->idf = 0;
    for (uint32_t i = 0, max = VA_Get_Size(terms); i < max; i++) {
        Obj     *term     = VA_Fetch(terms, i);
        int32_t  doc_max  = Searcher_Doc_Max(searcher);
        int32_t  doc_freq = Searcher_Doc_Freq(searcher, parent_ivars->field, term);
        ivars->idf += Sim_IDF(sim, doc_freq, doc_max);
    }

    // Calculate raw weight.
    ivars->raw_weight = ivars->idf * ivars->boost;

    return self;
}

void
PhraseCompiler_serialize(PhraseCompiler *self, OutStream *outstream) {
    PhraseCompilerIVARS *const ivars = PhraseCompiler_IVARS(self);
    Compiler_serialize((Compiler*)self, outstream);
    OutStream_Write_F32(outstream, ivars->idf);
    OutStream_Write_F32(outstream, ivars->raw_weight);
    OutStream_Write_F32(outstream, ivars->query_norm_factor);
    OutStream_Write_F32(outstream, ivars->normalized_weight);
}

PhraseCompiler*
PhraseCompiler_deserialize(PhraseCompiler *self, InStream *instream) {
    PhraseCompiler_Deserialize_t super_deserialize
        = SUPER_METHOD_PTR(PHRASECOMPILER, Lucy_PhraseCompiler_Deserialize);
    self = super_deserialize(self, instream);
    PhraseCompilerIVARS *const ivars = PhraseCompiler_IVARS(self);
    ivars->idf               = InStream_Read_F32(instream);
    ivars->raw_weight        = InStream_Read_F32(instream);
    ivars->query_norm_factor = InStream_Read_F32(instream);
    ivars->normalized_weight = InStream_Read_F32(instream);
    return self;
}

bool
PhraseCompiler_equals(PhraseCompiler *self, Obj *other) {
    if (!Obj_Is_A(other, PHRASECOMPILER))                     { return false; }
    if (!Compiler_equals((Compiler*)self, other))             { return false; }
    PhraseCompilerIVARS *const ivars = PhraseCompiler_IVARS(self);
    PhraseCompilerIVARS *const ovars
        = PhraseCompiler_IVARS((PhraseCompiler*)other);
    if (ivars->idf != ovars->idf)                             { return false; }
    if (ivars->raw_weight != ovars->raw_weight)               { return false; }
    if (ivars->query_norm_factor != ovars->query_norm_factor) { return false; }
    if (ivars->normalized_weight != ovars->normalized_weight) { return false; }
    return true;
}

float
PhraseCompiler_get_weight(PhraseCompiler *self) {
    return PhraseCompiler_IVARS(self)->normalized_weight;
}

float
PhraseCompiler_sum_of_squared_weights(PhraseCompiler *self) {
    PhraseCompilerIVARS *const ivars = PhraseCompiler_IVARS(self);
    return ivars->raw_weight * ivars->raw_weight;
}

void
PhraseCompiler_apply_norm_factor(PhraseCompiler *self, float factor) {
    PhraseCompilerIVARS *const ivars = PhraseCompiler_IVARS(self);
    ivars->query_norm_factor = factor;
    ivars->normalized_weight = ivars->raw_weight * ivars->idf * factor;
}

Matcher*
PhraseCompiler_make_matcher(PhraseCompiler *self, SegReader *reader,
                            bool need_score) {
    UNUSED_VAR(need_score);
    PhraseCompilerIVARS *const ivars = PhraseCompiler_IVARS(self);
    PhraseQueryIVARS *const parent_ivars
        = PhraseQuery_IVARS((PhraseQuery*)ivars->parent);
    VArray *const      terms     = parent_ivars->terms;
    uint32_t           num_terms = VA_Get_Size(terms);

    // Bail if there are no terms.
    if (!num_terms) { return NULL; }

    // Bail unless field is valid and posting type supports positions.
    Similarity *sim     = PhraseCompiler_Get_Similarity(self);
    Posting    *posting = Sim_Make_Posting(sim);
    if (posting == NULL || !Obj_Is_A((Obj*)posting, SCOREPOSTING)) {
        DECREF(posting);
        return NULL;
    }
    DECREF(posting);

    // Bail if there's no PostingListReader for this segment.
    PostingListReader *const plist_reader
        = (PostingListReader*)SegReader_Fetch(
              reader, VTable_Get_Name(POSTINGLISTREADER));
    if (!plist_reader) { return NULL; }

    // Look up each term.
    VArray  *plists = VA_new(num_terms);
    for (uint32_t i = 0; i < num_terms; i++) {
        Obj *term = VA_Fetch(terms, i);
        PostingList *plist
            = PListReader_Posting_List(plist_reader, parent_ivars->field, term);

        // Bail if any one of the terms isn't in the index.
        if (!plist || !PList_Get_Doc_Freq(plist)) {
            DECREF(plist);
            DECREF(plists);
            return NULL;
        }
        VA_Push(plists, (Obj*)plist);
    }

    Matcher *retval
        = (Matcher*)PhraseMatcher_new(sim, plists, (Compiler*)self);
    DECREF(plists);
    return retval;
}

VArray*
PhraseCompiler_highlight_spans(PhraseCompiler *self, Searcher *searcher,
                               DocVector *doc_vec, const CharBuf *field) {
    PhraseCompilerIVARS *const ivars = PhraseCompiler_IVARS(self);
    PhraseQueryIVARS *const parent_ivars
        = PhraseQuery_IVARS((PhraseQuery*)ivars->parent);
    VArray *const      terms     = parent_ivars->terms;
    VArray *const      spans     = VA_new(0);
    const uint32_t     num_terms = VA_Get_Size(terms);
    UNUSED_VAR(searcher);

    // Bail if no terms or field doesn't match.
    if (!num_terms) { return spans; }
    if (!CB_Equals(field, (Obj*)parent_ivars->field)) { return spans; }

    VArray *term_vectors    = VA_new(num_terms);
    BitVector *posit_vec       = BitVec_new(0);
    BitVector *other_posit_vec = BitVec_new(0);
    for (uint32_t i = 0; i < num_terms; i++) {
        Obj *term = VA_Fetch(terms, i);
        TermVector *term_vector
            = DocVec_Term_Vector(doc_vec, field, (CharBuf*)term);

        // Bail if any term is missing.
        if (!term_vector) {
            break;
        }

        VA_Push(term_vectors, (Obj*)term_vector);

        if (i == 0) {
            // Set initial positions from first term.
            I32Array *positions = TV_Get_Positions(term_vector);
            for (uint32_t j = I32Arr_Get_Size(positions); j > 0; j--) {
                BitVec_Set(posit_vec, I32Arr_Get(positions, j - 1));
            }
        }
        else {
            // Filter positions using logical "and".
            I32Array *positions = TV_Get_Positions(term_vector);

            BitVec_Clear_All(other_posit_vec);
            for (uint32_t j = I32Arr_Get_Size(positions); j > 0; j--) {
                int32_t pos = I32Arr_Get(positions, j - 1) - i;
                if (pos >= 0) {
                    BitVec_Set(other_posit_vec, pos);
                }
            }
            BitVec_And(posit_vec, other_posit_vec);
        }
    }

    // Proceed only if all terms are present.
    uint32_t num_tvs = VA_Get_Size(term_vectors);
    if (num_tvs == num_terms) {
        TermVector *first_tv = (TermVector*)VA_Fetch(term_vectors, 0);
        TermVector *last_tv
            = (TermVector*)VA_Fetch(term_vectors, num_tvs - 1);
        I32Array *tv_start_positions = TV_Get_Positions(first_tv);
        I32Array *tv_end_positions   = TV_Get_Positions(last_tv);
        I32Array *tv_start_offsets   = TV_Get_Start_Offsets(first_tv);
        I32Array *tv_end_offsets     = TV_Get_End_Offsets(last_tv);
        uint32_t  terms_max          = num_terms - 1;
        I32Array *valid_posits       = BitVec_To_Array(posit_vec);
        uint32_t  num_valid_posits   = I32Arr_Get_Size(valid_posits);
        uint32_t j = 0;
        float weight = PhraseCompiler_Get_Weight(self);
        uint32_t i = 0;

        // Add only those starts/ends that belong to a valid position.
        for (uint32_t posit_tick = 0; posit_tick < num_valid_posits; posit_tick++) {
            int32_t valid_start_posit = I32Arr_Get(valid_posits, posit_tick);
            int32_t valid_end_posit   = valid_start_posit + terms_max;
            int32_t start_offset = 0, end_offset = 0;

            for (uint32_t max = I32Arr_Get_Size(tv_start_positions); i < max; i++) {
                if (I32Arr_Get(tv_start_positions, i) == valid_start_posit) {
                    start_offset = I32Arr_Get(tv_start_offsets, i);
                    break;
                }
            }
            for (uint32_t max = I32Arr_Get_Size(tv_end_positions); j < max; j++) {
                if (I32Arr_Get(tv_end_positions, j) == valid_end_posit) {
                    end_offset = I32Arr_Get(tv_end_offsets, j);
                    break;
                }
            }

            VA_Push(spans, (Obj*)Span_new(start_offset,
                                          end_offset - start_offset, weight));

            i++, j++;
        }

        DECREF(valid_posits);
    }

    DECREF(other_posit_vec);
    DECREF(posit_vec);
    DECREF(term_vectors);
    return spans;
}


