# Setting Up and Using MinGW with PowerShell or Command Prompt

This document provides clear instructions to compile and run a C++ program using MinGW in PowerShell or Command Prompt, along with the expected output for clarity.

## Step-by-Step Instructions

### 1. Verify MinGW Installation

- Ensure MinGW is installed at `C:\Users\dlandry\AppData\Local\mingw64`.
- Confirm that `g++.exe` exists in the `bin` folder (`C:\Users\dlandry\AppData\Local\mingw64\bin`).

### 2. Update PATH Temporarily in PowerShell or Command Prompt

To use MinGW without permanently changing the PATH environment variable:

#### For PowerShell:

```powershell
$env:PATH="C:\Users\dlandry\AppData\Local\mingw64\bin;" + $env:PATH
```

#### For Command Prompt:

```cmd
SET PATH=C:\Users\dlandry\AppData\Local\mingw64\bin;%PATH%
```

Verify the compiler is recognized by checking its version:

#### For PowerShell or Command Prompt:

```bash
g++ --version
```

Expected Output:

```
g++ (MinGW <version>)
Copyright (C) <year> Free Software Foundation, Inc.
...
```

### 3. Compile the C++ Program

Use the following command to compile your C++ file:

```bash
g++ -std=c++17 -Ofast -march=native -flto -fomit-frame-pointer -fno-exceptions -fno-rtti -DNDEBUG -o fast_scan4 fast_scan4-no_filter.cpp
```

### Explanation of Options

- `-std=c++17`: Use the C++17 standard for compilation.
- `-Ofast`: Enable aggressive optimizations that may disregard strict standards compliance for better performance.
- `-march=native`: Optimize code for the architecture of the machine compiling the code by enabling all CPU-specific instructions.
- `-flto`: Enable Link-Time Optimization, allowing for cross-file optimizations.
- `-fomit-frame-pointer`: Remove the frame pointer to save registers and slightly improve performance (not recommended if debugging).
- `-fno-exceptions`: Disable exception handling support to reduce binary size and increase speed.
- `-fno-rtti`: Disable Run-Time Type Information (RTTI), further reducing binary size and improving speed.
- `-DNDEBUG`: Define the `NDEBUG` macro to disable assertions, often used for production builds.
- `-o fast_scan4`: Specify the output file name for the executable (`fast_scan4`).

### 4. Run the Executable

Once compiled, you can run the executable with the help command:

```bash
fast_scan4.exe --help
```

### 5. Expected Help Output

When you run the `--help` command, you should see the following usage instructions:

```
Usage: file_scanner --path=<root_path> [--prefix=<folder_prefix>] [--buffer=<buffer_size_kb>] [--output=<output_file>]

Options:
  --path     Path to the root directory to scan (required).
  --prefix   Filter for top-level folders to include in the scan.
             Only folders starting with this prefix will be scanned.
  --buffer   Output buffer size in KB (default: 5000 lines).
  --output   Name of the output file (default: file_list.csv).
  --help     Display this help message.
```

### 6. Permanently Update PATH (Optional)

To permanently add MinGW to your PATH:

1. Open **System Properties** > **Advanced** > **Environment Variables**.
2. Under "System Variables," find `Path` and click **Edit**.
3. Add the following to the list:
   ```
   C:\Users\dlandry\AppData\Local\mingw64\bin
   ```
4. Restart PowerShell or Command Prompt and verify with:
   ```bash
   g++ --version
   ```

### 7. Troubleshooting

- If `g++` is not recognized:
  - Double-check the PATH includes the correct folder.
  - Ensure MinGW is properly installed.
  - Use the full path to `g++.exe` if necessary:
    ```bash
    "C:\Users\dlandry\AppData\Local\mingw64\bin\g++.exe" -std=c++17 -Ofast -march=native -flto -fomit-frame-pointer -fno-exceptions -fno-rtti -DNDEBUG -o fast_scan4 fast_scan4-no_filter.cpp
    ```


