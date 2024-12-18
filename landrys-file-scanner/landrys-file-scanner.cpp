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

//----------------------------------------------------------
// Data structures and global settings
//----------------------------------------------------------

// Holds all scanning context shared across threads
struct ScanContext
{
    std::wstring ROOT_DIR;
    std::wstring PREFIX = L"";
    size_t OUTPUT_BUFFER_FLUSH_COUNT = 5000; // Default buffer size in lines
    std::string OUTPUT_FILE = "file_list.csv";
    std::vector<std::wstring> file_types;

    std::mutex q_m;
    std::condition_variable q_cv;
    std::queue<std::wstring> dir_queue;
    std::atomic<int> active_dir_count{0};
    std::atomic<bool> done{false};

    std::mutex out_m;
    FILE *out_fp = nullptr;

    std::atomic<long long> file_count{0};
};

static const int NUM_THREADS = std::thread::hardware_concurrency();

//----------------------------------------------------------
// Function Declarations
//----------------------------------------------------------
void print_help();
bool parse_arguments(int argc, char *argv[], ScanContext &ctx);
bool initialize_directory_queue(ScanContext &ctx);
void flush_buffer(ScanContext &ctx, std::string &buffer);
void process_directory(ScanContext &ctx, const std::wstring &dir, std::string &local_out_buf);
void directory_processing_worker(ScanContext &ctx);

//----------------------------------------------------------
// Function Implementations
//----------------------------------------------------------

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

bool parse_arguments(int argc, char *argv[], ScanContext &ctx)
{
    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg.find("--path=") == 0)
        {
            ctx.ROOT_DIR = std::wstring(arg.begin() + 7, arg.end());
        }
        else if (arg.find("--prefix=") == 0)
        {
            ctx.PREFIX = std::wstring(arg.begin() + 9, arg.end());
        }
        else if (arg.find("--buffer=") == 0)
        {
            ctx.OUTPUT_BUFFER_FLUSH_COUNT = std::stoul(arg.substr(9)) * 1000 / 256;
        }
        else if (arg.find("--output=") == 0)
        {
            ctx.OUTPUT_FILE = arg.substr(9);
        }
        else if (arg.find("--filetypes=") == 0)
        {
            std::wstring extensions = std::wstring(arg.begin() + 12, arg.end());
            size_t pos = 0;
            while ((pos = extensions.find(L",")) != std::wstring::npos)
            {
                ctx.file_types.push_back(extensions.substr(0, pos));
                extensions.erase(0, pos + 1);
            }
            ctx.file_types.push_back(extensions);
        }
        else if (arg == "--help")
        {
            print_help();
            return false;
        }
    }

    if (ctx.ROOT_DIR.empty())
    {
        std::cerr << "Error: --path is required.\n\n";
        print_help();
        return false;
    }

    return true;
}

// Initializes the directory queue with the top-level directories that match PREFIX
bool initialize_directory_queue(ScanContext &ctx)
{
    WIN32_FIND_DATAW fdata;
    std::wstring top_search = ctx.ROOT_DIR + L"\\*";
    HANDLE hFind = FindFirstFileW(top_search.c_str(), &fdata);
    if (hFind == INVALID_HANDLE_VALUE)
    {
        return false;
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

            if (ctx.PREFIX.empty() || _wcsnicmp(fdata.cFileName, ctx.PREFIX.c_str(), ctx.PREFIX.size()) == 0)
            {
                std::wstring subdir = ctx.ROOT_DIR + L"\\" + fdata.cFileName;
                {
                    std::lock_guard<std::mutex> lk(ctx.q_m);
                    ctx.dir_queue.push(subdir);
                    ctx.active_dir_count++;
                }
            }
        }
    } while (FindNextFileW(hFind, &fdata));
    FindClose(hFind);

    return (ctx.active_dir_count > 0);
}

// Flushes the local buffer to the output file safely
void flush_buffer(ScanContext &ctx, std::string &buffer)
{
    std::lock_guard<std::mutex> lk_out(ctx.out_m);
    fwrite(buffer.data(), 1, buffer.size(), ctx.out_fp);
    buffer.clear();
}

// Processes a single directory: finds subdirectories (pushing them to queue)
// and files (writing them to output if they match conditions)
void process_directory(ScanContext &ctx, const std::wstring &dir, std::string &local_out_buf)
{
    WIN32_FIND_DATAW fdata;
    std::wstring search_pattern = dir + L"\\*";
    HANDLE hFind = FindFirstFileExW(search_pattern.c_str(), FindExInfoBasic, &fdata, FindExSearchNameMatch, NULL, FIND_FIRST_EX_LARGE_FETCH);

    if (hFind == INVALID_HANDLE_VALUE)
    {
        ctx.active_dir_count--;
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
            if (!ctx.PREFIX.empty() && subdir.find(ctx.PREFIX) == std::wstring::npos)
            {
                continue;
            }

            {
                std::lock_guard<std::mutex> lk(ctx.q_m);
                ctx.dir_queue.push(subdir);
                ctx.active_dir_count++;
            }
            ctx.q_cv.notify_one();
        }
        else
        {
            std::wstring full_path = dir + L"\\" + fdata.cFileName;

            // File extension filtering
            if (!ctx.file_types.empty())
            {
                std::wstring file_ext = full_path.substr(full_path.find_last_of(L".") + 1);
                bool match = false;
                for (const auto &ext : ctx.file_types)
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
                std::string utf8_path(utf8_len, '\0');
                WideCharToMultiByte(CP_UTF8, 0, full_path.c_str(), slen, utf8_path.data(), utf8_len, NULL, NULL);

                // Add to the output buffer with a newline
                local_out_buf += utf8_path + "\n";

                ctx.file_count.fetch_add(1, std::memory_order_relaxed);

                // Flush if buffer is large enough
                if (local_out_buf.size() >= ctx.OUTPUT_BUFFER_FLUSH_COUNT * 256)
                {
                    flush_buffer(ctx, local_out_buf);
                }
            }
            else
            {
                // Log the error or handle the file path gracefully
                std::cerr << "Error converting file path to UTF-8: " << GetLastError() << "\n";
            }
        }
    } while (FindNextFileW(hFind, &fdata));
    FindClose(hFind);

    ctx.active_dir_count--;
}

// The main worker thread function that continuously processes directories from the queue
void directory_processing_worker(ScanContext &ctx)
{
    std::string local_out_buf;
    local_out_buf.reserve(256 * ctx.OUTPUT_BUFFER_FLUSH_COUNT);

    for (;;)
    {
        std::wstring current_dir;
        {
            std::unique_lock<std::mutex> lk(ctx.q_m);
            ctx.q_cv.wait(lk, [&]
                          { return !ctx.dir_queue.empty() || ctx.done.load(); });

            if (ctx.done.load() && ctx.dir_queue.empty())
            {
                // No more directories to process
                break;
            }

            if (!ctx.dir_queue.empty())
            {
                current_dir = ctx.dir_queue.front();
                ctx.dir_queue.pop();
            }
        }

        if (!current_dir.empty())
        {
            process_directory(ctx, current_dir, local_out_buf);
        }
    }

    // Flush remaining buffer
    if (!local_out_buf.empty())
    {
        flush_buffer(ctx, local_out_buf);
    }
}

//----------------------------------------------------------
// Main
//----------------------------------------------------------
int main(int argc, char *argv[])
{
    ScanContext ctx;
    if (!parse_arguments(argc, argv, ctx))
    {
        // Help or error message already printed
        return 1;
    }

    auto start_time = std::chrono::steady_clock::now();

    ctx.out_fp = fopen(ctx.OUTPUT_FILE.c_str(), "wb");
    if (!ctx.out_fp)
    {
        std::cerr << "Failed to open output file.\n";
        return 1;
    }

    // Write BOM for UTF-8
    const unsigned char bom[] = {0xEF, 0xBB, 0xBF};
    fwrite(bom, sizeof(bom), 1, ctx.out_fp);

    // Write CSV header
    const char *header = "File Path\n";
    fwrite(header, 1, strlen(header), ctx.out_fp);

    // Initialize the directory queue
    if (!initialize_directory_queue(ctx))
    {
        fclose(ctx.out_fp);
        std::cout << "No matching directories found.\n";
        return 0;
    }

    // Launch worker threads
    std::vector<std::thread> threads;
    threads.reserve(NUM_THREADS);
    for (int i = 0; i < NUM_THREADS; i++)
    {
        threads.emplace_back(directory_processing_worker, std::ref(ctx));
    }

    // Wait until all directories are processed
    for (;;)
    {
        std::unique_lock<std::mutex> lk(ctx.q_m);
        if (ctx.active_dir_count.load() == 0 && ctx.dir_queue.empty())
            break;
        ctx.q_cv.wait_for(lk, std::chrono::milliseconds(50));
    }

    // Signal threads to finish
    ctx.done.store(true);
    ctx.q_cv.notify_all();

    for (auto &t : threads)
        t.join();

    fclose(ctx.out_fp);

    auto end_time = std::chrono::steady_clock::now();
    double elapsed_seconds = std::chrono::duration<double>(end_time - start_time).count();
    long long final_count = ctx.file_count.load();

    std::cout << "File list export completed in " << elapsed_seconds << " seconds\n";
    std::cout << "Processed " << final_count << " files\n";
    if (elapsed_seconds > 0)
    {
        std::cout << "Average processing speed: " << (double)final_count / elapsed_seconds << " files/second\n";
    }

    return 0;
}
