// Copyright (C) 2019 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "android/emulation/control/snapshot/TarStream.h"
#include <fstream>
#include <sstream>

#include <gtest/gtest.h>  // for Test, Message, TestP...
#include "android/base/files/PathUtils.h"
#include "android/base/testing/TestSystem.h"

#include "android/emulation/control/snapshot/GzipStreambuf.h"

using android::base::PathUtils;
using android::base::pj;
using android::base::System;
using android::base::TestSystem;
using android::base::TestTempDir;

namespace android {
namespace emulation {
namespace control {

class TarStreamTest : public testing::Test {
protected:
    TarStreamTest() : mTestSystem("/", System::kProgramBitness) {}

    void SetUp() {
        auto testDir = mTestSystem.getTempRoot();
        testDir->makeSubDir("in");
        testDir->makeSubDir("out");
        indir = pj(testDir->path(), "in");
        outdir = pj(testDir->path(), "out");
        std::string hi = pj(indir, "hello.txt");
        std::string hi2 = pj(indir, "hello2.txt");

        std::ofstream hello(hi, std::ios_base::binary);
        hello << "Hello World!";
        hello.close();

        std::ofstream hello2(hi2, std::ios_base::binary);
        hello2 << "World! How are you?";
        hello2.close();
    }

    bool is_equal(std::string file1, std::string file2) {
        std::ifstream ifs1(file1, std::ios::binary);
        std::ifstream ifs2(file2, std::ios::binary);
        char buf1[512] = {0};
        char buf2[512] = {0};
        bool same = true;
        while (same && ifs1.good() && ifs2.good()) {
            ifs1.read(buf1, sizeof(buf1));
            ifs2.read(buf2, sizeof(buf2));

            same = memcmp(buf1, buf2, sizeof(buf1)) == 0;
        }

        return same && (ifs1.good() == ifs2.good());
    }

    TestSystem mTestSystem;
    std::string indir;
    std::string outdir;
};

TEST_F(TarStreamTest, read_write_dir) {
    std::stringstream ss;
    TarWriter tw(mTestSystem.getTempRoot()->path(), ss);
    EXPECT_TRUE(tw.addDirectoryEntry("in"));
    EXPECT_TRUE(tw.close());

    TarReader tr(outdir, ss);
    auto fst = tr.first();

    EXPECT_TRUE(fst.valid);
    EXPECT_STREQ(fst.name.c_str(), "in");
    EXPECT_EQ(fst.type, TarType::DIRTYPE);
    EXPECT_TRUE(tr.extract(fst));
    EXPECT_TRUE(mTestSystem.host()->pathIsDir(pj(outdir, "in")));
}

TEST_F(TarStreamTest, read_write_id) {
    std::stringstream ss;
    TarWriter tw(indir, ss);
    EXPECT_TRUE(tw.addFileEntry("hello.txt"));
    EXPECT_TRUE(tw.close());

    TarReader tr(outdir, ss);
    auto fst = tr.first();

    EXPECT_TRUE(fst.valid);
    EXPECT_STREQ(fst.name.c_str(), "hello.txt");
    EXPECT_EQ(fst.type, TarType::REGTYPE);
    EXPECT_TRUE(tr.extract(fst));
    EXPECT_TRUE(is_equal(pj(indir, "hello.txt"), pj(outdir, "hello.txt")));
}

TEST_F(TarStreamTest, read_write_gzip_id) {
    std::stringstream ss;
    GzipOutputStream gos(ss);
    TarWriter tw(indir, gos);
    EXPECT_TRUE(tw.addFileEntry("hello.txt"));
    EXPECT_TRUE(tw.close());

    GzipInputStream gis(ss);
    TarReader tr(outdir, gis);
    auto fst = tr.first();

    EXPECT_TRUE(fst.valid);
    EXPECT_STREQ(fst.name.c_str(), "hello.txt");
    EXPECT_EQ(fst.type, TarType::REGTYPE);
    EXPECT_TRUE(tr.extract(fst));
    EXPECT_TRUE(is_equal(pj(indir, "hello.txt"), pj(outdir, "hello.txt")));
}

TEST_F(TarStreamTest, tar_can_list) {
    // Make sure native tar utility can read the contents..
    std::string tar = pj(outdir, "test.tar");
    std::ofstream tarfile(tar);
    TarWriter tw(indir, tarfile);
    EXPECT_TRUE(tw.addFileEntry("hello.txt"));
    EXPECT_TRUE(tw.close());
    tarfile.close();

    auto result = mTestSystem.host()->runCommandWithResult({"tar", "tf", tar});
    if (result) {
        EXPECT_NE(result->find("hello.txt"), std::string::npos);
    }
}

TEST_F(TarStreamTest, tar_can_extract) {
    // Make sure native tar utility can read the contents..
    std::string tar = pj(outdir, "test.tar");
    std::ofstream tarfile(tar);
    TarWriter tw(indir, tarfile);
    EXPECT_TRUE(tw.addFileEntry("hello.txt"));
    EXPECT_TRUE(tw.close());
    tarfile.close();

    // untar the things..
    auto currdir = mTestSystem.host()->getCurrentDirectory();
    mTestSystem.host()->setCurrentDirectory(outdir);
    auto result = mTestSystem.host()->runCommandWithResult({"tar", "xf", tar});
    mTestSystem.host()->setCurrentDirectory(currdir);

    if (result) {
        EXPECT_TRUE(is_equal(pj(indir, "hello.txt"), pj(outdir, "hello.txt")));
    }
}

TEST_F(TarStreamTest, tar_can_extract_multiple_files) {
    // Make sure native tar utility can read the contents..
    std::string tar = pj(outdir, "test.tar");
    std::ofstream tarfile(tar);
    TarWriter tw(indir, tarfile);
    EXPECT_TRUE(tw.addFileEntry("hello.txt"));
    EXPECT_TRUE(tw.addFileEntry("hello2.txt"));
    EXPECT_TRUE(tw.close());
    tarfile.close();

    // untar the things..
    auto currdir = mTestSystem.host()->getCurrentDirectory();
    mTestSystem.host()->setCurrentDirectory(outdir);
    auto result = mTestSystem.host()->runCommandWithResult({"tar", "xf", tar});
    mTestSystem.host()->setCurrentDirectory(currdir);

    if (result) {
        EXPECT_TRUE(is_equal(pj(indir, "hello.txt"), pj(outdir, "hello.txt")));
        EXPECT_TRUE(
                is_equal(pj(indir, "hello2.txt"), pj(outdir, "hello2.txt")));
    }
}

TEST_F(TarStreamTest, stream_can_extract_tar) {
    // Make sure we can handle tar files created by tar..
    std::string tar = pj(outdir, "test.tar");

    // tar the things..
    auto currdir = mTestSystem.host()->getCurrentDirectory();
    mTestSystem.host()->setCurrentDirectory(indir);
    auto result = mTestSystem.host()->runCommandWithResult(
            {"tar", "cf", tar, "hello.txt"});
    mTestSystem.host()->setCurrentDirectory(currdir);

    if (result) {
        std::ifstream tarfile(tar);
        TarReader tr(outdir, tarfile);
        auto fst = tr.first();

        EXPECT_TRUE(fst.valid);
        EXPECT_STREQ(fst.name.c_str(), "hello.txt");
        EXPECT_TRUE(tr.extract(fst));
        EXPECT_TRUE(is_equal(pj(indir, "hello.txt"), pj(outdir, "hello.txt")));
    }
}

}  // namespace control
}  // namespace emulation
}  // namespace android
