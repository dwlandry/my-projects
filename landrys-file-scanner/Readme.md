# File Scanner

## Overview

The File Scanner is a multithreaded tool written in C++ that efficiently scans directories and files, allowing users to filter results by file types, folder prefixes, and more. The tool is optimized for performance and utilizes all available CPU cores to ensure fast processing.

### Key Features

- Multithreaded directory traversal using a thread-safe queue.
- Configurable filtering by file types and folder prefixes.
- Outputs results to a CSV file.
- Customizable buffer size for efficient file writing.
- Displays processing statistics, including total files processed and speed.

## Usage

### Command-Line Options

```
Usage: file_scanner --path=<root_path> [options]

Options:
  --path       Path to the root directory to scan (required).
  --prefix     Filter for top-level folders to include in the scan.
               Only folders starting with this prefix will be scanned.
  --buffer     Output buffer size in KB (default: 5000 lines).
  --output     Name of the output file (default: file_list.csv).
  --filetypes  Comma-separated list of file extensions to include (e.g., doc,docx,pdf).
               If not provided, all files will be included.
  --help       Display this help message.
```

### Examples

#### Basic Usage

Scan all files in the directory `C:\Data`:

```bash
file_scanner --path=C:\Data
```

#### Filter by Folder Prefix

Scan only folders starting with `Proj`:

```bash
file_scanner --path=C:\Data --prefix=Proj
```

#### Filter by File Types

Include only files with `.doc` or `.pdf` extensions:

```bash
file_scanner --path=C:\Data --filetypes=doc,pdf
```

#### Custom Output File and Buffer Size

Set the output file to `output.csv` and use a buffer size of 10,000 lines:

```bash
file_scanner --path=C:\Data --output=output.csv --buffer=10000
```

## Output

The tool generates a CSV file containing the paths of the files that match the specified criteria. The default file name is `file_list.csv`, but you can customize it using the `--output` option.

## Building the Project

### Prerequisites

- Windows OS
- C++ compiler (e.g., MSVC, MinGW)

### Steps

1. Clone the repository:

   ```bash
   git clone <repository_url>
   cd file_scanner
   ```

2. Compile the code:

   ```bash
   g++ -std=c++17 -Ofast -march=native -flto -fomit-frame-pointer -fno-exceptions -fno-rtti -DNDEBUG -o file_scanner file_scanner.cpp
   ```

### Explanation of Compilation Options

- `-std=c++17`: Use the C++17 standard for compilation.
- `-Ofast`: Enable aggressive optimizations that may disregard strict standards compliance for better performance.
- `-march=native`: Optimize code for the architecture of the machine compiling the code by enabling all CPU-specific instructions.
- `-flto`: Enable Link-Time Optimization, allowing for cross-file optimizations.
- `-fomit-frame-pointer`: Remove the frame pointer to save registers and slightly improve performance (not recommended if debugging).
- `-fno-exceptions`: Disable exception handling support to reduce binary size and increase speed.
- `-fno-rtti`: Disable Run-Time Type Information (RTTI), further reducing binary size and improving speed.
- `-DNDEBUG`: Define the `NDEBUG` macro to disable assertions, often used for production builds.

### Running the Program

After building the project, run the executable with the desired options:

```bash
file_scanner --path=C:\Data
```

### Troubleshooting

- If `g++` is not recognized:
  - Ensure MinGW is properly installed.
  - Use the full path to `g++.exe` if necessary:
    ```bash
    "C:\Users\<username>\AppData\Local\mingw64\bin\g++.exe" -std=c++17 -Ofast -march=native -flto -fomit-frame-pointer -fno-exceptions -fno-rtti -DNDEBUG -o file_scanner file_scanner.cpp
    ```

## Performance

The tool is optimized to utilize all available CPU cores. It dynamically balances the workload among threads to ensure efficient processing of large directory structures.

## Contribution

Contributions are welcome! If you have any suggestions or improvements, please submit a pull request or open an issue in the repository.

## License

This project is licensed under the MIT License. See the `LICENSE` file for details.

