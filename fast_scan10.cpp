#include <windows.h>
#include <cstdio>
#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <chrono>
#include <iostream>
#include <algorithm>

// Default values
static std::wstring ROOT_DIR;
static std::wstring PREFIX = L"";
static size_t OUTPUT_BUFFER_FLUSH_COUNT = 5000; // Default buffer size in lines
static std::string OUTPUT_FILE = "file_list.csv";
static std::vector<std::wstring> file_types;

static const int NUM_THREADS = std::thread::hardware_concurrency();

std::mutex q_m;
std::condition_variable q_cv;
std::queue<std::wstring> dir_queue;
std::atomic<int> active_dir_count(0);
std::atomic<bool> done(false);

std::mutex out_m;
FILE *out_fp = nullptr;

std::atomic<long long> file_count(0);

void print_help()
{
    std::cout << "Usage: file_scanner --path=<root_path> [--prefix=<folder_prefix>] "
                 "[--buffer=<buffer_size_kb>] [--output=<output_file>] [--filetypes=<extensions>]\n\n"
                 "Options:\n"
                 "  --path       Path to the root directory to scan (required).\n"
                 "  --prefix     Filter for top-level folders to include in the scan.\n"
                 "               Only folders starting with this prefix will be scanned.\n"
                 "  --buffer     Output buffer size in KB (default: 5000 lines).\n"
                 "  --output     Name of the output file (default: file_list.csv).\n"
                 "  --filetypes  Comma-separated list of file extensions to include (e.g., doc,docx,pdf).\n"
                 "               If not provided, all files will be included.\n"
                 "  --help       Display this help message.\n";
}

void process_directory(const std::wstring &dir, std::string &local_out_buf)
{
    WIN32_FIND_DATAW fdata;
    std::wstring search_pattern = dir + L"\\*";
    HANDLE hFind = FindFirstFileExW(search_pattern.c_str(), FindExInfoBasic, &fdata, FindExSearchNameMatch, NULL, FIND_FIRST_EX_LARGE_FETCH);

    if (hFind == INVALID_HANDLE_VALUE)
    {
        active_dir_count--;
        return;
    }

    do
    {
        if ((fdata.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        {
            // Skip '.' and '..'
            if (fdata.cFileName[0] == L'.' &&
                (fdata.cFileName[1] == 0 || (fdata.cFileName[1] == L'.' && fdata.cFileName[2] == 0)))
            {
                continue;
            }

            std::wstring subdir = dir + L"\\" + fdata.cFileName;
            // Check prefix if specified
            if (!PREFIX.empty() && subdir.find(PREFIX) == std::wstring::npos)
            {
                continue;
            }

            {
                std::lock_guard<std::mutex> lk(q_m);
                dir_queue.push(subdir);
                active_dir_count++;
            }
            q_cv.notify_one();
        }
        else
        {
            std::wstring full_path = dir + L"\\" + fdata.cFileName;

            // File extension filtering
            if (!file_types.empty())
            {
                std::wstring file_ext = full_path.substr(full_path.find_last_of(L".") + 1);
                bool match = false;
                for (const auto &ext : file_types)
                {
                    if (_wcsicmp(file_ext.c_str(), ext.c_str()) == 0)
                    {
                        match = true;
                        break;
                    }
                }
                if (!match)
                    continue;
            }

            // Convert to UTF-8 and add to output buffer
            int slen = (int)full_path.size();
            int utf8_len = WideCharToMultiByte(CP_UTF8, 0, full_path.c_str(), slen, NULL, 0, NULL, NULL);

            if (utf8_len > 0)
            {
                size_t old_size = local_out_buf.size();
                local_out_buf.resize(old_size + utf8_len + 1);
                WideCharToMultiByte(CP_UTF8, 0, full_path.c_str(), slen, &local_out_buf[old_size], utf8_len, NULL, NULL);
                local_out_buf[old_size + utf8_len] = '\n';

                file_count.fetch_add(1, std::memory_order_relaxed);

                // Flush if the buffer is large enough
                if (local_out_buf.size() >= OUTPUT_BUFFER_FLUSH_COUNT * 256)
                {
                    std::lock_guard<std::mutex> lk_out(out_m);
                    fwrite(local_out_buf.data(), 1, local_out_buf.size(), out_fp);
                    local_out_buf.clear();
                }
            }
        }
    } while (FindNextFileW(hFind, &fdata));
    FindClose(hFind);

    active_dir_count--;
}

void worker_thread()
{
    std::string local_out_buf;
    local_out_buf.reserve(256 * OUTPUT_BUFFER_FLUSH_COUNT);

    for (;;)
    {
        std::wstring current_dir;
        {
            std::unique_lock<std::mutex> lk(q_m);
            q_cv.wait(lk, []
                      { return !dir_queue.empty() || done.load(); });

            if (done.load() && dir_queue.empty())
            {
                // No more directories to process
                break;
            }

            if (!dir_queue.empty())
            {
                current_dir = dir_queue.front();
                dir_queue.pop();
            }
        }

        if (!current_dir.empty())
        {
            process_directory(current_dir, local_out_buf);
        }
    }

    // Flush remaining buffer
    if (!local_out_buf.empty())
    {
        std::lock_guard<std::mutex> lk_out(out_m);
        fwrite(local_out_buf.data(), 1, local_out_buf.size(), out_fp);
    }
}

int main(int argc, char *argv[])
{
    // Parse command-line arguments
    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg.find("--path=") == 0)
        {
            ROOT_DIR = std::wstring(arg.begin() + 7, arg.end());
        }
        else if (arg.find("--prefix=") == 0)
        {
            PREFIX = std::wstring(arg.begin() + 9, arg.end());
        }
        else if (arg.find("--buffer=") == 0)
        {
            OUTPUT_BUFFER_FLUSH_COUNT = std::stoul(arg.substr(9)) * 1000 / 256;
        }
        else if (arg.find("--output=") == 0)
        {
            OUTPUT_FILE = arg.substr(9);
        }
        else if (arg.find("--filetypes=") == 0)
        {
            std::wstring extensions = std::wstring(arg.begin() + 12, arg.end());
            size_t pos = 0;
            while ((pos = extensions.find(L",")) != std::wstring::npos)
            {
                file_types.push_back(extensions.substr(0, pos));
                extensions.erase(0, pos + 1);
            }
            file_types.push_back(extensions);
        }
        else if (arg == "--help")
        {
            print_help();
            return 0;
        }
    }

    if (ROOT_DIR.empty())
    {
        std::cerr << "Error: --path is required.\n\n";
        print_help();
        return 1;
    }

    auto start_time = std::chrono::steady_clock::now();

    out_fp = fopen(OUTPUT_FILE.c_str(), "wb");
    if (!out_fp)
    {
        printf("Failed to open output file.\n");
        return 1;
    }

    const char *header = "File Path\n";
    fwrite(header, 1, strlen(header), out_fp);

    // Initialize directory queue with initial directories
    {
        WIN32_FIND_DATAW fdata;
        std::wstring top_search = ROOT_DIR + L"\\*";
        HANDLE hFind = FindFirstFileW(top_search.c_str(), &fdata);
        if (hFind != INVALID_HANDLE_VALUE)
        {
            do
            {
                if ((fdata.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
                {
                    // Skip '.' and '..'
                    if (fdata.cFileName[0] == L'.' &&
                        (fdata.cFileName[1] == 0 || (fdata.cFileName[1] == L'.' && fdata.cFileName[2] == 0)))
                    {
                        continue;
                    }

                    // Check prefix
                    if (PREFIX.empty() || _wcsnicmp(fdata.cFileName, PREFIX.c_str(), PREFIX.size()) == 0)
                    {
                        std::wstring subdir = ROOT_DIR + L"\\" + fdata.cFileName;
                        {
                            std::lock_guard<std::mutex> lk(q_m);
                            dir_queue.push(subdir);
                            active_dir_count++;
                        }
                    }
                }
            } while (FindNextFileW(hFind, &fdata));
            FindClose(hFind);
        }
    }

    if (active_dir_count == 0)
    {
        fclose(out_fp);
        printf("No matching directories found.\n");
        return 0;
    }

    std::vector<std::thread> threads;
    for (int i = 0; i < NUM_THREADS; i++)
    {
        threads.emplace_back(worker_thread);
    }

    // Wait until all directories are processed
    for (;;)
    {
        std::unique_lock<std::mutex> lk(q_m);
        if (active_dir_count.load() == 0 && dir_queue.empty())
            break;
        q_cv.wait_for(lk, std::chrono::milliseconds(50));
    }

    // Signal threads to finish
    done.store(true);
    q_cv.notify_all();

    for (auto &t : threads)
        t.join();

    fclose(out_fp);

    auto end_time = std::chrono::steady_clock::now();
    double elapsed_seconds = std::chrono::duration<double>(end_time - start_time).count();
    long long final_count = file_count.load();

    printf("File list export completed in %.2f seconds\n", elapsed_seconds);
    printf("Processed %lld files\n", final_count);
    if (elapsed_seconds > 0)
    {
        printf("Average processing speed: %.2f files/second\n", (double)final_count / elapsed_seconds);
    }

    return 0;
}