// Copyright (c) 2022 Jack Zhang <zjgoggle@gmail.com>.
// Licensed under the MIT License <http://opensource.org/licenses/MIT>.
// SPDX-License-Identifier: MIT
//
// Permission is hereby  granted, free of charge, to any  person obtaining a copy
// of this software and associated  documentation files (the "Software"), to deal
// in the Software  without restriction, including without  limitation the rights
// to  use, copy,  modify, merge,  publish, distribute,  sublicense, and/or  sell
// copies  of  the Software,  and  to  permit persons  to  whom  the Software  is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE  IS PROVIDED "AS  IS", WITHOUT WARRANTY  OF ANY KIND,  EXPRESS OR
// IMPLIED,  INCLUDING BUT  NOT  LIMITED TO  THE  WARRANTIES OF  MERCHANTABILITY,
// FITNESS FOR  A PARTICULAR PURPOSE AND  NONINFRINGEMENT. IN NO EVENT  SHALL THE
// AUTHORS  OR COPYRIGHT  HOLDERS  BE  LIABLE FOR  ANY  CLAIM,  DAMAGES OR  OTHER
// LIABILITY, WHETHER IN AN ACTION OF  CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE  OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once

#include <unordered_map>
#include <iostream>
#include <string>
#include <algorithm>
#include <vector>
#include <chrono>
#include <memory>
#include <iomanip>
#include <assert.h>

#if defined(_MSC_VER)
#include <intrin.h>
#pragma intrinsic(__rdtsc)
#define __PRETTY_FUNCTION__ __FUNCSIG__
#endif

// auto report stats when reaching JZPROFILER_DEFAULT_SAMPLES.
// it can be changed by modifying JzProfilerStore::instance()._params.nSamples. Then subsequent JZ_PROF_FUNC() will be affected.
#ifndef JZPROFILER_DEFAULT_SAMPLES
#define JZPROFILER_DEFAULT_SAMPLES 10000
#endif

inline int64_t get_cpu_ticks() {
#if defined(__i386__)
    int64_t ret;
    __asm__ volatile("rdtsc" : "=A"(ret));
    return ret;
#elif defined(__x86_64__) || defined(__amd64__)
    uint64_t low, high;
    __asm__ volatile("rdtsc" : "=a"(low), "=d"(high));
    return (high << 32) | low;
#elif defined(__ia64__)
    int64_t itc;
    asm("mov %0 = ar.itc" : "=r"(itc));
    return itc;
#elif defined(COMPILER_MSVC) && defined(_M_IX86)
    // #elif defined( _MSC_VER )
    // Older MSVC compilers (like 7.x) don't seem to support the
    // __rdtsc intrinsic properly, so I prefer to use _asm instead
    // when I know it will work.  Otherwise, I'll use __rdtsc and hope
    // the code is being compiled with a non-ancient compiler.
    _asm rdtsc
    // #elif defined( COMPILER_MSVC )
#elif defined(_MSC_VER)
    return __rdtsc();
#elif defined(__aarch64__)
    // System timer of ARMv8 runs at a different frequency than the CPU's.
    // The frequency is fixed, typically in the range 1-50MHz.  It can be
    // read at CNTFRQ special register.  We assume the OS has set up
    // the virtual timer properly.
    int64_t virtual_timer_value;
    asm volatile("mrs %0, cntvct_el0" : "=r"(virtual_timer_value));
    return virtual_timer_value;
#else
#error Please implement rdtsc
#endif
}

inline float calcCPUTicksPerNano() {
    static constexpr int64_t DURATION_NANOS = 10000;
    auto                     t              = std::chrono::steady_clock().now().time_since_epoch().count() + DURATION_NANOS;
    auto                     c              = get_cpu_ticks();
    while (std::chrono::steady_clock().now().time_since_epoch().count() < t) continue;
    c = get_cpu_ticks() - c;
    return float(c) / DURATION_NANOS;
}

struct JzSrcLocation {
    const char *filename; // string literal
    int         line;
    const char *function;           // string literal
    const char *profname = nullptr; // string literal, maybe nullptr

    std::string srcLine() const { return filename + std::string(":") + std::to_string(line); }
    std::string funcString() const { return std::string(function) + (profname ? (std::string(" : ") + profname) : std::string()); }
};

struct JzProfilerParams {
    size_t        nSamples;
    float         ticksPerNano;
    std::ostream *ostream = nullptr;
};

struct JzProfiler {
    JzProfiler(JzSrcLocation loc, const JzProfilerParams &params) : _loc(loc), _params(params) {
        assert(_params.nSamples > 1);
        _samples.reserve(_params.nSamples + 1);
        _samples.resize(1); // allocate one initially
    }
    void        reset() { _samples.resize(1); }
    static void printStatsHeaderLine(std::ostream &os) {
        os << "Samples\t  Min/Nanos\t\t  50% \t\t  99% \t\t  Max \t| \t location \t | \t function" << std::endl;
    }
    std::ostream &printLocFields() const {
        *_params.ostream << _loc.srcLine() << " | " << _loc.funcString();
        return *_params.ostream;
    }
    std::ostream &printLevelInfo(int depth, int idx) const {
        for (auto i = 0; i < depth; ++i) *_params.ostream << "    ";
        if (depth) *_params.ostream << " [" << idx << "] ";
        return *_params.ostream;
    }
    bool empty() const { return _samples.size() <= 1; }
    void reportStatsAndReset(int depth = -1, int idx = -1) {
        if (empty()) return;
        size_t N = _samples.size() - 1;
        std::sort(_samples.begin(), --_samples.end());
        int w = 10;
        *_params.ostream << std::setw(4) << N << "\t" << std::setw(w) << nanosAt(0) << "\t" << std::setw(w) << nanosAt(N / 2) << "\t" << std::setw(w)
                         << nanosAt(N * 0.99) << "\t" << std::setw(w)
                         << nanosAt(_samples.size() - (N == 1 ? 2 : 3)); // the second largest one as the largest.
        *_params.ostream << "\t| ";
        if (depth >= 0) printLevelInfo(depth, idx);
        printLocFields();
        *_params.ostream << std::endl;
        reset();
    }
    void startRecord() { _samples.back() = get_cpu_ticks(); }
    void stopRecord() {
        _samples.back() = get_cpu_ticks() - _samples.back();
        if (_samples.size() == _params.nSamples) reportStatsAndReset();
        else _samples.emplace_back();
    }
    int64_t nanosAt(size_t idx) const { return _samples[std::min(idx, _samples.size() - 2)] / _params.ticksPerNano; }

    JzSrcLocation        _loc;
    JzProfilerParams     _params; // the last element is always ready for next startRecord().
    std::vector<int64_t> _samples;
    //
    struct LevelInfo {
        JzProfiler *parent = nullptr;
        int         depth  = 0;
        int         nChild = 0;
    };

    void      setLevel(const LevelInfo &level) { _levelInfo = level; }
    LevelInfo _levelInfo; // only used to save temp info, not used to print.
};

struct JzProfilerStore {
    JzProfilerParams                            _params;
    std::unordered_map<std::string, JzProfiler> _profilers;

    JzProfilerStore(size_t nSamples = JZPROFILER_DEFAULT_SAMPLES, std::ostream &os = std::cout) : _params{nSamples, calcCPUTicksPerNano(), &os} {}

    ~JzProfilerStore() { reportStatsAndReset(); }


    // \pre The location has not been added before.
    JzProfiler *add(JzSrcLocation loc) {
        // maydo: throws if location has been added before.
        auto res = _profilers.insert(std::make_pair(loc.srcLine(), JzProfiler(loc, _params)));
        assert(res.second);
        return &res.first->second;
    }

    void reportStatsAndReset() {
        struct My {
            bool bPrintedHeader = false;
            void printRec(std::vector<ProfilerTreePtr> &trees, int depth = 0) {
                int idx = 0;
                for (auto &e : trees) {
                    if (!bPrintedHeader && !e->value->empty()) {
                        JzProfiler::printStatsHeaderLine(*e->value->_params.ostream);
                        bPrintedHeader = true;
                    }
                    e->value->reportStatsAndReset(depth, idx++);
                    printRec(e->children, depth + 1);
                }
            }
        };
        static std::vector<ProfilerTreePtr> trees = buildProfilerTree(); // static trees to make sure it been built once.

        My my;
        my.printRec(trees);
    }

    static JzProfilerStore &instance() {
        static JzProfilerStore _inst;
        return _inst;
    }

private:
    struct ProfilerTree {
        JzProfiler                                *value = nullptr;
        std::vector<std::unique_ptr<ProfilerTree>> children;
    };
    using ProfilerTreePtr = std::unique_ptr<ProfilerTree>;

    std::vector<ProfilerTreePtr> buildProfilerTree() {
        struct My {
            static ProfilerTree *buildTree(std::unordered_map<JzProfiler *, ProfilerTree *> &treeByProf,
                                           std::vector<ProfilerTreePtr>                     &trees,
                                           JzProfiler                                       *curr) {
                assert(curr);
                auto it = treeByProf.find(curr);
                if (it != treeByProf.end()) // current is already in tree.
                    return it->second;
                std::vector<std::unique_ptr<ProfilerTree>> *owner;
                if (curr->_levelInfo.parent) {
                    auto parentTree = buildTree(treeByProf, trees, curr->_levelInfo.parent);
                    // now parent has been built.
                    owner = &parentTree->children;
                } else // no parent
                {
                    owner = &trees;
                }
                owner->push_back(std::make_unique<ProfilerTree>());
                owner->back()->value = curr;
                treeByProf[curr]     = owner->back().get();
                return owner->back().get();
            }
            static void sortTreesRec(std::vector<ProfilerTreePtr> &trees) {
                std::sort(trees.begin(), trees.end(), [](const ProfilerTreePtr &x, const ProfilerTreePtr &y) {
                    return x->value->_levelInfo.nChild < y->value->_levelInfo.nChild;
                });
                for (auto &e : trees) { sortTreesRec(e->children); }
            }
        };
        std::vector<ProfilerTreePtr>                     trees;
        std::unordered_map<JzProfiler *, ProfilerTree *> treeByProf; // temp

        for (auto &e : _profilers) { My::buildTree(treeByProf, trees, &e.second); }
        // sort children
        My::sortTreesRec(trees);

        return trees;
    }
};

// single-threaded
struct JzScopedProfRecorder {
    JzProfiler           *_prof;
    JzProfiler::LevelInfo _parent;

    static JzProfiler::LevelInfo &currentLevel() {
        static JzProfiler::LevelInfo s_level;
        return s_level;
    }

    JzScopedProfRecorder(JzProfiler *prof) : _prof(prof) {
        // save parent
        auto &currlevel = currentLevel();
        _prof->setLevel(currlevel);
        _parent = currlevel;
        // update current level
        currlevel.parent = prof;
        currlevel.depth++;

        _prof->startRecord();
    }

    ~JzScopedProfRecorder() {
        _prof->stopRecord();
        // restore parent
        auto &currlevel = currentLevel();
        currlevel       = _parent;
        currlevel.nChild++;
    }
};

#ifndef NDEBUG
#define ENABLE_JZ_PROF
#else
#undef ENABLE_JZ_PROF
#endif

#if defined(ENABLE_JZ_PROF) || defined(OVERRIDE_ENABLE_JZ_PROF)

#define JZ_PROF_CONCAT_IMPL(a, b) a##b
#define JZ_PROF_CONCAT(a, b) JZ_PROF_CONCAT_IMPL(a, b)

//-- JZ_PROF_SCOPE
#define JZ_PROF_SCOPE_IMPL(profname, c)                                                                                                               \
    static auto         *JZ_PROF_CONCAT(JzProfiler_scope, c) = JzProfilerStore::instance().add({__FILE__, __LINE__, __PRETTY_FUNCTION__, #profname}); \
    JzScopedProfRecorder JZ_PROF_CONCAT(JzProfiler_scoperecorder, c)(JZ_PROF_CONCAT(JzProfiler_scope, c))

#define JZ_PROF_SCOPE(profname) JZ_PROF_SCOPE_IMPL(profname, __COUNTER__)

//-- JZ_PROF_FUNC
#define JZ_PROF_FUNC_IMPL(c)                                                                                                                        \
    static auto *JZ_PROF_CONCAT(JzProfiler_func, c) = JzProfilerStore::instance().add(JzSrcLocation{__FILE__, __LINE__, __PRETTY_FUNCTION__});      \
    JzScopedProfRecorder JZ_PROF_CONCAT(JzProfiler_funcrecorder, c)(JZ_PROF_CONCAT(JzProfiler_func, c))

#define JZ_PROF_FUNC() JZ_PROF_FUNC_IMPL(__COUNTER__)

//-- JZ_PROF_NAMED_START, JZ_PROF_NAMED_STOP must in a scope that the defined static profiler is accessible.
#define JZ_PROF_NAMED_START(profname)                                                                                                               \
    static auto *JZ_PROF_CONCAT(JzProfiler_named, profname) =                                                                                       \
            JzProfilerStore::instance().add({__FILE__, __LINE__, __PRETTY_FUNCTION__, #profname});                                                  \
    JZ_PROF_CONCAT(JzProfiler_named, profname)->startRecord()

#define JZ_PROF_NAMED_STOP(profname) JZ_PROF_CONCAT(JzProfiler_named, profname)->stopRecord()


#else // undefined ENABLE_JZ_PROF

#define JZ_PROF_SCOPE(profname)
#define JZ_PROF_FUNC()
#define JZ_PROF_NAMED_START(profname)
#define JZ_PROF_NAMED_STOP(profname)

#endif // ENABLE_JZ_PROF

//-- debug&release JZ_PROF_ADD: add a profiler. return a pointer to added profiler.
#define JZ_PROF_ADD(profname) JzProfilerStore::instance().add({__FILE__, __LINE__, __PRETTY_FUNCTION__, #profname})

#define JZ_PROF_GLOBAL(profname) JzProfilerStore::instance().add({__FILE__, __LINE__, "", #profname})
