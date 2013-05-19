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
#include <stdio.h>

#include "Lucy/Util/ToolSet.h"

#include "Lucy/Util/Json.h"
#include "Lucy/Store/Folder.h"
#include "Lucy/Store/InStream.h"
#include "Lucy/Store/OutStream.h"
#include "Clownfish/Util/Memory.h"
#include "Lucy/Util/Json/JsonParser.h"

/* Routines generated by Lemon. */
void*
LucyParseJsonAlloc(void * (*allocate)(size_t));
void
LucyParseJson(void *json_parser, int token_type, Obj *value,
              lucy_JsonParserState *state);
void
LucyParseJsonFree(void *json_parser, void(*freemem)(void*));
void
LucyParseJsonTrace(FILE *trace, char *line_prefix);

// Encode JSON for supplied "dump".  On failure, sets Err_error and returns
// false.
static bool
S_to_json(Obj *dump, CharBuf *json, int32_t depth);

// Parse JSON from raw UTF-8 in memory.
static Obj*
S_parse_json(char *text, size_t size);
static Obj*
S_do_parse_json(void *json_parser, char *json, size_t len);

// Parse a JSON number.  Advance the text buffer just past the number.
static Float64*
S_parse_number(char **json_ptr, char *const limit);

// Parse a JSON string.  Advance the text buffer from pointing at the opening
// double quote to pointing just after the closing double quote.
static CharBuf*
S_parse_string(char **json_ptr, char *const limit);

// Unescape JSON string text.  Expects pointers bookending the text data (i.e.
// pointing just after the opening double quote and directly at the closing
// double quote), and assumes that escapes have already been sanity checked
// for length.
static CharBuf*
S_unescape_text(char *const top, char *const end);

// Check that the supplied text begins with the specified keyword, which must
// then end on a word boundary (i.e. match "null" but not the first four
// letters of "nullify").
static INLINE bool
SI_check_keyword(char *json, char* end, const char *keyword, size_t len);

// Make it possible to be loosen constraints during testing.
static bool tolerant = false;

// Indentation: two spaces per level.
static const char indentation[]     = "  ";
static const size_t INDENTATION_LEN = sizeof(indentation) - 1;

// Append indentation spaces x depth.
static void
S_cat_whitespace(CharBuf *json, int32_t depth);

// Set Err_error, appending escaped JSON in the vicinity of the error.
static void
S_set_error(CharBuf *mess, char *json, char *limit, int line,
            const char *func);
#define SET_ERROR(_mess, _json, _end) \
    S_set_error(_mess, _json, _end, __LINE__, CFISH_ERR_FUNC_MACRO)

Obj*
Json_from_json(CharBuf *json) {
    Obj *dump = S_parse_json((char*)CB_Get_Ptr8(json), CB_Get_Size(json));
    if (!dump) {
        ERR_ADD_FRAME(Err_get_error());
    }
    return dump;
}

Obj*
Json_slurp_json(Folder *folder, const CharBuf *path) {
    InStream *instream = Folder_Open_In(folder, path);
    if (!instream) {
        ERR_ADD_FRAME(Err_get_error());
        return NULL;
    }
    size_t len = (size_t)InStream_Length(instream);
    char *buf = InStream_Buf(instream, len);
    Obj *dump = S_parse_json(buf, len);
    InStream_Close(instream);
    DECREF(instream);
    if (!dump) {
        ERR_ADD_FRAME(Err_get_error());
    }
    return dump;
}

bool
Json_spew_json(Obj *dump, Folder *folder, const CharBuf *path) {
    CharBuf *json = Json_to_json(dump);
    if (!json) {
        ERR_ADD_FRAME(Err_get_error());
        return false;
    }
    OutStream *outstream = Folder_Open_Out(folder, path);
    if (!outstream) {
        ERR_ADD_FRAME(Err_get_error());
        DECREF(json);
        return false;
    }
    size_t size = CB_Get_Size(json);
    OutStream_Write_Bytes(outstream, CB_Get_Ptr8(json), size);
    OutStream_Close(outstream);
    DECREF(outstream);
    DECREF(json);
    return true;
}

CharBuf*
Json_to_json(Obj *dump) {
    // Validate object type, only allowing hashes and arrays per JSON spec.
    if (!dump || !(Obj_Is_A(dump, HASH) || Obj_Is_A(dump, VARRAY))) {
        if (!tolerant) {
            CharBuf *class_name = dump ? Obj_Get_Class_Name(dump) : NULL;
            CharBuf *mess = MAKE_MESS("Illegal top-level object type: %o",
                                      class_name);
            Err_set_error(Err_new(mess));
            return NULL;
        }
    }

    // Encode.
    CharBuf *json = CB_new(31);
    if (!S_to_json(dump, json, 0)) {
        DECREF(json);
        ERR_ADD_FRAME(Err_get_error());
        json = NULL;
    }
    else {
        // Append newline.
        CB_Cat_Trusted_Str(json, "\n", 1);
    }

    return json;
}

void
Json_set_tolerant(bool tolerance) {
    tolerant = tolerance;
}

static const int32_t MAX_DEPTH = 200;

static void
S_append_json_string(Obj *dump, CharBuf *json) {
    // Append opening quote.
    CB_Cat_Trusted_Str(json, "\"", 1);

    // Process string data.
    ZombieCharBuf *iterator = ZCB_WRAP((CharBuf*)dump);
    while (ZCB_Get_Size(iterator)) {
        uint32_t code_point = ZCB_Nip_One(iterator);
        if (code_point > 127) {
            // There is no need to escape any high characters, including those
            // above the BMP, as we assume that the destination channel can
            // handle arbitrary UTF-8 data.
            CB_Cat_Char(json, code_point);
        }
        else {
            char buffer[7];
            size_t len;
            switch (code_point & 127) {
                    // Perform all mandatory escapes enumerated in the JSON spec.
                    // Note that the spec makes escaping forward slash optional;
                    // we choose not to.
                case 0x00: case 0x01: case 0x02: case 0x03:
                case 0x04: case 0x05: case 0x06: case 0x07:
                case 0x0b: case 0x0e: case 0x0f:
                case 0x10: case 0x11: case 0x12: case 0x13:
                case 0x14: case 0x15: case 0x16: case 0x17:
                case 0x18: case 0x19: case 0x1a: case 0x1b:
                case 0x1c: case 0x1d: case 0x1e: case 0x1f: {
                        sprintf(buffer, "\\u%04x", (unsigned)code_point);
                        len = 6;
                        break;
                    }
                case '\b':
                    memcpy(buffer, "\\b", 2);
                    len = 2;
                    break;
                case '\t':
                    memcpy(buffer, "\\t", 2);
                    len = 2;
                    break;
                case '\n':
                    memcpy(buffer, "\\n", 2);
                    len = 2;
                    break;
                case '\f':
                    memcpy(buffer, "\\f", 2);
                    len = 2;
                    break;
                case '\r':
                    memcpy(buffer, "\\r", 2);
                    len = 2;
                    break;
                case '\\':
                    memcpy(buffer, "\\\\", 2);
                    len = 2;
                    break;
                case '\"':
                    memcpy(buffer, "\\\"", 2);
                    len = 2;
                    break;

                    // Ordinary printable ASCII.
                default:
                    buffer[0] = (char)code_point;
                    len = 1;
            }
            CB_Cat_Trusted_Str(json, buffer, len);
        }
    }

    // Append closing quote.
    CB_Cat_Trusted_Str(json, "\"", 1);
}

static void
S_cat_whitespace(CharBuf *json, int32_t depth) {
    while (depth--) {
        CB_Cat_Trusted_Str(json, indentation, INDENTATION_LEN);
    }
}

static bool
S_to_json(Obj *dump, CharBuf *json, int32_t depth) {
    // Guard against infinite recursion in self-referencing data structures.
    if (depth > MAX_DEPTH) {
        CharBuf *mess = MAKE_MESS("Exceeded max depth of %i32", MAX_DEPTH);
        Err_set_error(Err_new(mess));
        return false;
    }

    if (!dump) {
        CB_Cat_Trusted_Str(json, "null", 4);
    }
    else if (dump == (Obj*)CFISH_TRUE) {
        CB_Cat_Trusted_Str(json, "true", 4);
    }
    else if (dump == (Obj*)CFISH_FALSE) {
        CB_Cat_Trusted_Str(json, "false", 5);
    }
    else if (Obj_Is_A(dump, CHARBUF)) {
        S_append_json_string(dump, json);
    }
    else if (Obj_Is_A(dump, INTNUM)) {
        CB_catf(json, "%i64", Obj_To_I64(dump));
    }
    else if (Obj_Is_A(dump, FLOATNUM)) {
        CB_catf(json, "%f64", Obj_To_F64(dump));
    }
    else if (Obj_Is_A(dump, VARRAY)) {
        VArray *array = (VArray*)dump;
        size_t size = VA_Get_Size(array);
        if (size == 0) {
            // Put empty array on single line.
            CB_Cat_Trusted_Str(json, "[]", 2);
            return true;
        }
        else if (size == 1) {
            Obj *elem = VA_Fetch(array, 0);
            if (!(Obj_Is_A(elem, HASH) || Obj_Is_A(elem, VARRAY))) {
                // Put array containing single scalar element on one line.
                CB_Cat_Trusted_Str(json, "[", 1);
                if (!S_to_json(elem, json, depth + 1)) {
                    return false;
                }
                CB_Cat_Trusted_Str(json, "]", 1);
                return true;
            }
        }
        // Fall back to spreading elements across multiple lines.
        CB_Cat_Trusted_Str(json, "[", 1);
        for (size_t i = 0; i < size; i++) {
            CB_Cat_Trusted_Str(json, "\n", 1);
            S_cat_whitespace(json, depth + 1);
            if (!S_to_json(VA_Fetch(array, i), json, depth + 1)) {
                return false;
            }
            if (i + 1 < size) {
                CB_Cat_Trusted_Str(json, ",", 1);
            }
        }
        CB_Cat_Trusted_Str(json, "\n", 1);
        S_cat_whitespace(json, depth);
        CB_Cat_Trusted_Str(json, "]", 1);
    }
    else if (Obj_Is_A(dump, HASH)) {
        Hash *hash = (Hash*)dump;
        size_t size = Hash_Get_Size(hash);

        // Put empty hash on single line.
        if (size == 0) {
            CB_Cat_Trusted_Str(json, "{}", 2);
            return true;
        }

        // Validate that all keys are strings, then sort.
        VArray *keys = Hash_Keys(hash);
        for (size_t i = 0; i < size; i++) {
            Obj *key = VA_Fetch(keys, i);
            if (!key || !Obj_Is_A(key, CHARBUF)) {
                DECREF(keys);
                CharBuf *key_class = key ? Obj_Get_Class_Name(key) : NULL;
                CharBuf *mess = MAKE_MESS("Illegal key type: %o", key_class);
                Err_set_error(Err_new(mess));
                return false;
            }
        }
        VA_Sort(keys, NULL, NULL);

        // Spread pairs across multiple lines.
        CB_Cat_Trusted_Str(json, "{", 1);
        for (size_t i = 0; i < size; i++) {
            Obj *key = VA_Fetch(keys, i);
            CB_Cat_Trusted_Str(json, "\n", 1);
            S_cat_whitespace(json, depth + 1);
            S_append_json_string(key, json);
            CB_Cat_Trusted_Str(json, ": ", 2);
            if (!S_to_json(Hash_Fetch(hash, key), json, depth + 1)) {
                DECREF(keys);
                return false;
            }
            if (i + 1 < size) {
                CB_Cat_Trusted_Str(json, ",", 1);
            }
        }
        CB_Cat_Trusted_Str(json, "\n", 1);
        S_cat_whitespace(json, depth);
        CB_Cat_Trusted_Str(json, "}", 1);

        DECREF(keys);
    }

    return true;
}

static Obj*
S_parse_json(char *text, size_t size) {
    void *json_parser = LucyParseJsonAlloc(Memory_wrapped_malloc);
    if (json_parser == NULL) {
        CharBuf *mess = MAKE_MESS("Failed to allocate JSON parser");
        Err_set_error(Err_new(mess));
        return NULL;
    }
    Obj *dump = S_do_parse_json(json_parser, text, size);
    LucyParseJsonFree(json_parser, Memory_wrapped_free);
    return dump;
}

static Obj*
S_do_parse_json(void *json_parser, char *json, size_t len) {
    lucy_JsonParserState state;
    state.result = NULL;
    state.errors = false;

    char *text = json;
    char *const end = text + len;
    while (text < end) {
        int  token_type = -1;
        Obj *value      = NULL;
        char *const save = text;
        switch (*text) {
            case ' ': case '\n': case '\r': case '\t':
                // Skip insignificant whitespace, which the JSON RFC defines
                // as only four ASCII characters.
                text++;
                continue;
            case '[':
                token_type = LUCY_JSON_TOKENTYPE_LEFT_SQUARE_BRACKET;
                text++;
                break;
            case ']':
                token_type = LUCY_JSON_TOKENTYPE_RIGHT_SQUARE_BRACKET;
                text++;
                break;
            case '{':
                token_type = LUCY_JSON_TOKENTYPE_LEFT_CURLY_BRACKET;
                text++;
                break;
            case '}':
                token_type = LUCY_JSON_TOKENTYPE_RIGHT_CURLY_BRACKET;
                text++;
                break;
            case ':':
                token_type = LUCY_JSON_TOKENTYPE_COLON;
                text++;
                break;
            case ',':
                token_type = LUCY_JSON_TOKENTYPE_COMMA;
                text++;
                break;
            case '"':
                value = (Obj*)S_parse_string(&text, end);
                if (value) {
                    token_type = LUCY_JSON_TOKENTYPE_STRING;
                }
                else {
                    // Clear out parser and return.
                    LucyParseJson(json_parser, 0, NULL, &state);
                    ERR_ADD_FRAME(Err_get_error());
                    return NULL;
                }
                break;
            case 'n':
                if (SI_check_keyword(text, end, "null", 4)) {
                    token_type = LUCY_JSON_TOKENTYPE_NULL;
                    text += 4;
                }
                break;
            case 't':
                if (SI_check_keyword(text, end, "true", 4)) {
                    token_type = LUCY_JSON_TOKENTYPE_TRUE;
                    value = (Obj*)CFISH_TRUE;
                    text += 4;
                }
                break;
            case 'f':
                if (SI_check_keyword(text, end, "false", 5)) {
                    token_type = LUCY_JSON_TOKENTYPE_FALSE;
                    value = (Obj*)CFISH_FALSE;
                    text += 5;
                }
                break;
            case '0': case '1': case '2': case '3': case '4':
            case '5': case '6': case '7': case '8': case '9':
            case '-': { // Note no '+', as JSON spec doesn't allow it.
                    value = (Obj*)S_parse_number(&text, end);
                    if (value) {
                        token_type = LUCY_JSON_TOKENTYPE_NUMBER;
                    }
                    else {
                        // Clear out parser and return.
                        LucyParseJson(json_parser, 0, NULL, &state);
                        ERR_ADD_FRAME(Err_get_error());
                        return NULL;
                    }
                }
                break;
        }
        LucyParseJson(json_parser, token_type, value, &state);
        if (state.errors) {
            SET_ERROR(CB_newf("JSON syntax error"), save, end);
            return NULL;
        }
    }

    // Finish up.
    LucyParseJson(json_parser, 0, NULL, &state);
    if (state.errors) {
        SET_ERROR(CB_newf("JSON syntax error"), json, end);
        return NULL;
    }
    return state.result;
}

static Float64*
S_parse_number(char **json_ptr, char *const limit) {
    char *top = *json_ptr;
    char *end = top;
    bool terminated = false;

    // We can't assume NULL termination for the JSON string, so we need to
    // ensure that strtod() cannot overrun and access invalid memory.
    for (; end < limit; end++) {
        switch (*end) {
                // Only these characters may legally follow a number in
                // Javascript.  If we don't find one before the end of the JSON,
                // it's a parse error.
            case ' ': case '\n': case '\r': case '\t':
            case ']':
            case '}':
            case ':':
            case ',':
                terminated = true;
                break;
        }
    }

    Float64 *result = NULL;
    if (terminated) {
        char *terminus;
        double number = strtod(top, &terminus);
        if (terminus != top) {
            *json_ptr = terminus;
            result = Float64_new(number);
        }
    }
    if (!result) {
        SET_ERROR(CB_newf("JSON syntax error"), top, limit);
    }
    return result;
}

static CharBuf*
S_parse_string(char **json_ptr, char *const limit) {
    // Find terminating double quote, determine whether there are any escapes.
    char *top = *json_ptr + 1;
    char *end = NULL;
    bool saw_backslash = false;
    for (char *text = top; text < limit; text++) {
        if (*text == '"') {
            end = text;
            break;
        }
        else if (*text == '\\') {
            saw_backslash = true;
            if (text + 1 < limit && text[1] == 'u') {
                text += 5;
            }
            else {
                text += 1;
            }
        }
    }
    if (!end) {
        SET_ERROR(CB_newf("Unterminated string"), *json_ptr, limit);
        return NULL;
    }

    // Advance the text buffer to just beyond the closing quote.
    *json_ptr = end + 1;

    if (saw_backslash) {
        return S_unescape_text(top, end);
    }
    else {
        // Optimize common case where there are no escapes.
        size_t len = end - top;
        if (!StrHelp_utf8_valid(top, len)) {
            CharBuf *mess = MAKE_MESS("Bad UTF-8 in JSON");
            Err_set_error(Err_new(mess));
            return NULL;
        }
        return CB_new_from_trusted_utf8(top, len);
    }
}

static CharBuf*
S_unescape_text(char *const top, char *const end) {
    // The unescaped string will never be longer than the escaped string
    // because only a \u escape can theoretically be too long and
    // StrHelp_encode_utf8_char guards against sequences over 4 bytes.
    // Therefore we can allocate once and not worry about reallocating.
    size_t cap = end - top + 1;
    char *target_buf = (char*)MALLOCATE(cap);
    size_t target_size = 0;
    for (char *text = top; text < end; text++) {
        if (*text != '\\') {
            target_buf[target_size++] = *text;
        }
        else {
            // Process escape.
            text++;
            switch (*text) {
                case '"':
                    target_buf[target_size++] = '"';
                    break;
                case '\\':
                    target_buf[target_size++] = '\\';
                    break;
                case '/':
                    target_buf[target_size++] = '/';
                    break;
                case 'b':
                    target_buf[target_size++] = '\b';
                    break;
                case 'f':
                    target_buf[target_size++] = '\f';
                    break;
                case 'n':
                    target_buf[target_size++] = '\n';
                    break;
                case 'r':
                    target_buf[target_size++] = '\r';
                    break;
                case 't':
                    target_buf[target_size++] = '\t';
                    break;
                case 'u': {
                        // Copy into a temp buffer because strtol will overrun
                        // into adjacent text data for e.g. "\uAAAA1".
                        char temp[5] = { 0, 0, 0, 0, 0 };
                        memcpy(temp, text + 1, 4);
                        text += 4;
                        char *num_end;
                        long code_point = strtol(temp, &num_end, 16);
                        char *temp_ptr = temp;
                        if (num_end != temp_ptr + 4 || code_point < 0) {
                            FREEMEM(target_buf);
                            SET_ERROR(CB_newf("Invalid \\u escape"), text - 5, end);
                            return NULL;
                        }
                        if (code_point >= 0xD800 && code_point <= 0xDFFF) {
                            FREEMEM(target_buf);
                            SET_ERROR(CB_newf("Surrogate pairs not supported"),
                                      text - 5, end);
                            return NULL;
                        }
                        target_size += StrHelp_encode_utf8_char((uint32_t)code_point,
                                                                target_buf + target_size);
                    }
                    break;
                default:
                    FREEMEM(target_buf);
                    SET_ERROR(CB_newf("Illegal escape"), text - 1, end);
                    return NULL;
            }
        }
    }

    // NULL-terminate, sanity check, then return the escaped string.
    target_buf[target_size] = '\0';
    if (!StrHelp_utf8_valid(target_buf, target_size)) {
        FREEMEM(target_buf);
        CharBuf *mess = MAKE_MESS("Bad UTF-8 in JSON");
        Err_set_error(Err_new(mess));
        return NULL;
    }
    return CB_new_steal_from_trusted_str(target_buf, target_size, cap);
}

static INLINE bool
SI_check_keyword(char *json, char* end, const char *keyword, size_t len) {
    if ((size_t)(end - json) > len
        && strncmp(json, keyword, len) == 0
        && json[len] != '_'
        && !isalnum(json[len])
       ) {
        return true;
    }
    return false;
}

static void
S_set_error(CharBuf *mess, char *json, char *limit, int line,
            const char *func) {
    if (func) {
        CB_catf(mess, " at %s %s line %i32 near ", func, __FILE__,
                (int32_t)line);
    }
    else {
        CB_catf(mess, " at %s line %i32 near ", __FILE__, (int32_t)line);
    }

    // Append escaped text.
    int64_t len = limit - json;
    if (len > 32) {
        const char *end = StrHelp_back_utf8_char(json + 32, json);
        len = end - json;
    }
    ZombieCharBuf *snippet = ZCB_WRAP_STR(json, len);
    S_append_json_string((Obj*)snippet, mess);

    // Set Err_error.
    Err_set_error(Err_new(mess));
}

