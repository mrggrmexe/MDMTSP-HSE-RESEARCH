#include "tsp_solver.hpp"

#include <memory>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <chrono>
#include <vector>
#include <string>
#include <limits>
#include <algorithm>
#include <utility>

#if !defined(_WIN32)
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#endif

namespace mdmtsp::tsp {

namespace fs = std::filesystem;

namespace {

struct TempDir {
    fs::path path;
    bool keep = false;

    explicit TempDir(std::string prefix = "mdmtsp_lkh_") {
        auto base = fs::temp_directory_path();
        std::uint64_t nonce = static_cast<std::uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count()
        );
        for (int i = 0; i < 128; ++i) {
            std::ostringstream oss;
            oss << prefix << nonce << "_" << i;
            fs::path p = base / oss.str();
            std::error_code ec;
            if (fs::create_directory(p, ec) && !ec) {
                path = std::move(p);
                return;
            }
        }
        throw std::runtime_error("failed to create temp directory");
    }

    ~TempDir() {
        if (keep || path.empty()) return;
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

inline bool file_exists_nonempty(const fs::path& p) {
    std::error_code ec;
    return fs::exists(p, ec) && !ec && fs::is_regular_file(p, ec) && !ec && fs::file_size(p, ec) > 0;
}

#if !defined(_WIN32)
inline bool is_executable(const fs::path& p) {
    std::error_code ec;
    if (!fs::exists(p, ec) || ec) return false;
    auto s = p.string();
    return ::access(s.c_str(), X_OK) == 0;
}
#else
inline bool is_executable(const fs::path& p) {
    std::error_code ec;
    return fs::exists(p, ec) && !ec;
}
#endif

inline std::vector<fs::path> candidate_exec_paths() {
    std::vector<fs::path> out;
    if (const char* env = std::getenv("LKH_EXECUTABLE"); env && *env) out.emplace_back(env);

    out.emplace_back("LKH");
    out.emplace_back("LKH-3");

    fs::path cwd = fs::current_path();
    out.emplace_back(cwd / "LKH");
    out.emplace_back(cwd / "external" / "lkh" / "LKH");
    out.emplace_back(cwd / "external" / "lkh" / "LKH-3");
    out.emplace_back(cwd / "external" / "LKH" / "LKH");
    out.emplace_back(cwd / "external" / "LKH-3" / "LKH");
    return out;
}

inline fs::path resolve_executable() {
    for (const auto& c : candidate_exec_paths()) {
        if (c.has_parent_path()) {
            if (is_executable(c)) return c;
        } else {
#if !defined(_WIN32)
            if (std::string s = c.string(); ::access(s.c_str(), X_OK) == 0) return c;
#else
            return c;
#endif
        }
    }
    return {};
}

inline std::string to_native(const fs::path& p) {
    return p.string();
}

inline bool safe_write_text(const fs::path& p, const std::string& s, std::string& err) {
    std::ofstream out(p, std::ios::out | std::ios::trunc);
    if (!out) {
        err = "cannot write file: " + to_native(p);
        return false;
    }
    out << s;
    if (!out.good()) {
        err = "failed writing file: " + to_native(p);
        return false;
    }
    return true;
}

inline bool safe_write_tsplib_full_matrix_atsp(const TSPProblem& prob,
                                               const TSPSolveOptions& opt,
                                               const fs::path& tsp_path,
                                               int scale,
                                               std::string& err) {
    const Index n = prob.n;

    std::ofstream out(tsp_path, std::ios::out | std::ios::trunc);
    if (!out) {
        err = "cannot write TSPLIB file";
        return false;
    }

    out << "NAME: MDMTSP_ATSP\n";
    out << "TYPE: ATSP\n";
    out << "DIMENSION: " << static_cast<std::uint64_t>(n) << "\n";
    out << "EDGE_WEIGHT_TYPE: EXPLICIT\n";
    out << "EDGE_WEIGHT_FORMAT: FULL_MATRIX\n";
    out << "EDGE_WEIGHT_SECTION\n";

    const long long cap = 1000000000LL;

    for (Index i = 0; i < n; ++i) {
        for (Index j = 0; j < n; ++j) {
            long long w = 0;
            if (i == j) {
                w = 0;
            } else {
                Cost c;
                try {
                    c = prob.dist(i, j);
                } catch (...) {
                    err = "distance function threw";
                    return false;
                }
                if (!is_finite(c)) {
                    err = "non-finite distance";
                    return false;
                }
                if (opt.require_non_negative_costs && c < 0) {
                    err = "negative distance";
                    return false;
                }
                const long double scaled = static_cast<long double>(c) * static_cast<long double>(scale);
                if (!std::isfinite(static_cast<double>(scaled))) {
                    err = "distance scaling overflow";
                    return false;
                }
                long long r = llround(scaled);
                if (r < 0) r = 0;
                if (r > cap) {
                    err = "scaled weight too large";
                    return false;
                }
                w = r;
            }
            out << w;
            if (j + 1 < n) out << ' ';
        }
        out << "\n";
        if (!out.good()) {
            err = "failed writing TSPLIB matrix";
            return false;
        }
    }

    out << "EOF\n";
    if (!out.good()) {
        err = "failed finalizing TSPLIB file";
        return false;
    }
    return true;
}

inline std::string make_param_file(const fs::path& problem_file,
                                   const fs::path& out_tour_file,
                                   const TSPSolveOptions& opt,
                                   Index n) {
    std::ostringstream ss;
    ss << "PROBLEM_FILE = " << to_native(problem_file) << "\n";
    ss << "OUTPUT_TOUR_FILE = " << to_native(out_tour_file) << "\n";
    ss << "RUNS = 1\n";
    ss << "TRACE_LEVEL = 0\n";

    std::uint64_t seed = opt.seed;
    if (seed == 0) seed = 1;
    ss << "SEED = " << seed << "\n";

    std::uint64_t max_trials = opt.iteration_limit;
    if (max_trials == 0) {
        max_trials = std::max<std::uint64_t>(1ULL, static_cast<std::uint64_t>(n) * 10ULL);
    }
    if (max_trials > 2000000000ULL) max_trials = 2000000000ULL;
    ss << "MAX_TRIALS = " << max_trials << "\n";

    if (opt.time_limit_ms != 0) {
        std::uint64_t sec = (opt.time_limit_ms + 999ULL) / 1000ULL;
        if (sec == 0) sec = 1;
        if (sec > 2000000000ULL) sec = 2000000000ULL;
        ss << "TIME_LIMIT = " << sec << "\n";
    }

    return ss.str();
}

#if !defined(_WIN32)
struct ProcResult {
    int exit_code = -1;
    bool timed_out = false;
    bool spawned = false;
};

inline ProcResult run_process_posix(const fs::path& exe,
                                   const fs::path& arg1,
                                   const fs::path& stdout_path,
                                   const fs::path& stderr_path,
                                   std::uint64_t time_limit_ms) {
    ProcResult pr;

    int out_fd = ::open(stdout_path.string().c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0644);
    int err_fd = ::open(stderr_path.string().c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if (out_fd < 0 || err_fd < 0) {
        if (out_fd >= 0) ::close(out_fd);
        if (err_fd >= 0) ::close(err_fd);
        return pr;
    }

    pid_t pid = ::fork();
    if (pid == 0) {
        ::dup2(out_fd, STDOUT_FILENO);
        ::dup2(err_fd, STDERR_FILENO);
        ::close(out_fd);
        ::close(err_fd);

        std::string exe_s = exe.string();
        std::string arg_s = arg1.string();

        char* argv[3];
        argv[0] = const_cast<char*>(exe_s.c_str());
        argv[1] = const_cast<char*>(arg_s.c_str());
        argv[2] = nullptr;

        ::execvp(argv[0], argv);
        _exit(127);
    }

    ::close(out_fd);
    ::close(err_fd);

    if (pid < 0) return pr;

    pr.spawned = true;

    const auto start = std::chrono::steady_clock::now();
    int status = 0;

    while (true) {
        pid_t r = ::waitpid(pid, &status, WNOHANG);
        if (r == pid) break;
        if (r == 0) {
            if (time_limit_ms != 0) {
                auto now = std::chrono::steady_clock::now();
                auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
                if (ms >= static_cast<long long>(time_limit_ms)) {
                    pr.timed_out = true;
                    ::kill(pid, SIGTERM);
                    for (int i = 0; i < 20; ++i) {
                        pid_t r2 = ::waitpid(pid, &status, WNOHANG);
                        if (r2 == pid) break;
                        ::usleep(50 * 1000);
                    }
                    ::kill(pid, SIGKILL);
                    ::waitpid(pid, &status, 0);
                    break;
                }
            }
            ::usleep(50 * 1000);
            continue;
        }
        pr.exit_code = -1;
        return pr;
    }

    if (WIFEXITED(status)) pr.exit_code = WEXITSTATUS(status);
    else if (WIFSIGNALED(status)) pr.exit_code = 128 + WTERMSIG(status);
    else pr.exit_code = -1;

    return pr;
}
#endif

inline bool parse_lkh_tour(const fs::path& tour_path, Index n, std::vector<Index>& out, std::string& err) {
    std::ifstream in(tour_path);
    if (!in) {
        err = "cannot open tour file";
        return false;
    }

    std::string line;
    bool in_section = false;
    std::vector<long long> nodes;
    nodes.reserve(static_cast<std::size_t>(n));

    while (std::getline(in, line)) {
        if (!in_section) {
            if (line.find("TOUR_SECTION") != std::string::npos) {
                in_section = true;
            }
            continue;
        } else {
            std::istringstream ss(line);
            long long v;
            while (ss >> v) {
                if (v == -1) goto done;
                if (v <= 0) continue;
                nodes.push_back(v);
            }
        }
    }

done:
    if (nodes.size() != static_cast<std::size_t>(n)) {
        err = "tour size mismatch";
        return false;
    }

    std::vector<std::uint8_t> seen(static_cast<std::size_t>(n), 0);
    out.clear();
    out.reserve(static_cast<std::size_t>(n));

    for (long long v1 : nodes) {
        if (v1 < 1 || v1 > static_cast<long long>(n)) {
            err = "tour contains out-of-range node";
            return false;
        }
        Index v = static_cast<Index>(v1 - 1);
        auto& s = seen[static_cast<std::size_t>(v)];
        if (s) {
            err = "tour contains duplicates";
            return false;
        }
        s = 1;
        out.push_back(v);
    }

    return true;
}

inline void rotate_to_start(std::vector<Index>& order, Index start) {
    auto it = std::find(order.begin(), order.end(), start);
    if (it == order.end()) return;
    std::rotate(order.begin(), it, order.end());
}

}  // namespace

class LKHSolver final : public TSPSolver {
public:
    std::string name() const override { return "lkh"; }

    TSPSolution solve(const TSPProblem& problem, const TSPSolveOptions& options) override {
        const auto t0 = std::chrono::steady_clock::now();

        const Status basic = problem.validate_basic();
        if (basic != Status::Ok) return make_failure(basic, name(), "invalid problem");

        if (problem.type != TourType::Cycle) {
            return make_failure(Status::NotImplemented, name(), "path variant not supported");
        }

        const Index n = problem.n;
        if (n == 0) return make_failure(Status::InvalidArgument, name(), "empty instance");
        if (n == 1) {
            std::vector<Index> order = {0};
            const auto t1 = std::chrono::steady_clock::now();
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
            return finalize_solution(problem, options, std::move(order), name(), static_cast<std::uint64_t>(elapsed));
        }

        fs::path exe = resolve_executable();
        if (exe.empty()) return make_failure(Status::InvalidArgument, name(), "LKH executable not found");

        TempDir tmp;
        const fs::path tsp_file  = tmp.path / "problem.atsp";
        const fs::path par_file  = tmp.path / "params.par";
        const fs::path tour_file = tmp.path / "out.tour";
        const fs::path log_out   = tmp.path / "stdout.txt";
        const fs::path log_err   = tmp.path / "stderr.txt";

        std::string err;

        int scales[] = {1000, 100, 10, 1};
        bool wrote = false;
        for (int sc : scales) {
            err.clear();
            if (safe_write_tsplib_full_matrix_atsp(problem, options, tsp_file, sc, err)) {
                wrote = true;
                break;
            }
        }
        if (!wrote) return make_failure(Status::InvalidDistance, name(), err.empty() ? "failed to write TSPLIB" : err);

        const std::string par = make_param_file(tsp_file, tour_file, options, n);
        if (!safe_write_text(par_file, par, err)) return make_failure(Status::InternalError, name(), err);

#if defined(_WIN32)
        return make_failure(Status::NotImplemented, name(), "process execution not implemented on Windows");
#else
        ProcResult pr = run_process_posix(exe, par_file, log_out, log_err, options.time_limit_ms);
        if (!pr.spawned) return make_failure(Status::InternalError, name(), "failed to spawn LKH");
        if (pr.timed_out) return make_failure(Status::TimeLimit, name(), "time limit");

        if (pr.exit_code != 0) {
            std::ostringstream msg;
            msg << "LKH exited with code " << pr.exit_code;
            return make_failure(Status::InternalError, name(), msg.str());
        }
#endif

        if (!file_exists_nonempty(tour_file)) {
            return make_failure(Status::InternalError, name(), "missing tour output");
        }

        std::vector<Index> order;
        if (!parse_lkh_tour(tour_file, n, order, err)) {
            return make_failure(Status::InternalError, name(), err.empty() ? "failed to parse tour" : err);
        }

        const auto maxv = std::numeric_limits<Index>::max();
        if (problem.start != maxv) rotate_to_start(order, problem.start);

        const auto t1 = std::chrono::steady_clock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

        return finalize_solution(problem, options, std::move(order), name(),
                                static_cast<std::uint64_t>(elapsed));
    }
};

std::unique_ptr<TSPSolver> make_lkh_solver() {
    return std::make_unique<LKHSolver>();
}

}  // namespace mdmtsp::tsp