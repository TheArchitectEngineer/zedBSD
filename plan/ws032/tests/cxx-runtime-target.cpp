#include <cstdio>
#include <string>
#include <vector>
#include <stdexcept>
#include <memory>

// A static initialiser: it must run before main.
struct Early { Early() { std::printf("CXX ok   static initialiser\n"); } };
static Early early;

// Thread-local storage with a non-trivial type, in the main executable.
thread_local std::string greeting = "thread local";

static int thrower(int depth) {
    if (depth == 0) throw std::runtime_error("unwound");
    return thrower(depth - 1) + 1;   // frames to unwind through
}

int main() {
    int checks = 0, failures = 0;
    auto check = [&](const char *name, bool ok) {
        checks++; if (!ok) failures++;
        std::printf("CXX %s %s\n", ok ? "ok  " : "FAIL", name);
    };

    std::vector<int> v{3, 1, 2};
    v.push_back(4);
    check("vector", v.size() == 4 && v[3] == 4);

    std::string s = "abc";
    s += "def";
    check("string", s == "abcdef" && s.find("cd") == 2);


    auto p = std::make_unique<std::vector<int>>(v);
    check("unique_ptr", p && p->size() == 4);

    check("thread_local", greeting == "thread local");

    bool caught = false;
    try { (void)thrower(5); } catch (const std::runtime_error &e) {
        caught = std::string(e.what()) == "unwound";
    }
    check("exception through 5 frames", caught);

    bool caught_bad = false;
    try { (void)v.at(99); } catch (const std::out_of_range &) { caught_bad = true; }
    check("out_of_range from the library", caught_bad);

    std::printf("CXX verdict: %s (%d/%d)\n",
                failures == 0 ? "PASS" : "FAIL", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
