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

#define TESTLUCY_USE_SHORT_NAMES
#include "Lucy/Util/ToolSet.h"

#include "Clownfish/TestHarness/TestBatchRunner.h"
#include "Lucy/Test.h"
#include "Lucy/Test/TestUtils.h"
#include "Lucy/Test/Analysis/TestAnalyzer.h"
#include "Lucy/Analysis/Analyzer.h"
#include "Lucy/Analysis/Inversion.h"

TestAnalyzer*
TestAnalyzer_new() {
    return (TestAnalyzer*)VTable_Make_Obj(TESTANALYZER);
}

DummyAnalyzer*
DummyAnalyzer_new() {
    DummyAnalyzer *self = (DummyAnalyzer*)VTable_Make_Obj(DUMMYANALYZER);
    return DummyAnalyzer_init(self);
}

DummyAnalyzer*
DummyAnalyzer_init(DummyAnalyzer *self) {
    return (DummyAnalyzer*)Analyzer_init((Analyzer*)self);
}

Inversion*
DummyAnalyzer_transform(DummyAnalyzer *self, Inversion *inversion) {
    UNUSED_VAR(self);
    return (Inversion*)INCREF(inversion);
}

static void
test_analysis(TestBatchRunner *runner) {
    DummyAnalyzer *analyzer = DummyAnalyzer_new();
    CharBuf *source = CB_newf("foo bar baz");
    VArray *wanted = VA_new(1);
    VA_Push(wanted, (Obj*)CB_newf("foo bar baz"));
    TestUtils_test_analyzer(runner, (Analyzer*)analyzer, source, wanted,
                            "test basic analysis");
    DECREF(wanted);
    DECREF(source);
    DECREF(analyzer);
}

void
TestAnalyzer_run(TestAnalyzer *self, TestBatchRunner *runner) {
    TestBatchRunner_Plan(runner, (TestBatch*)self, 3);
    test_analysis(runner);
}



