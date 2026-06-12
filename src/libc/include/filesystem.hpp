#ifndef _LIBC_FILESYSTEM_H
#define _LIBC_FILESYSTEM_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Filesystem configuration constants
enum FileSystemConstants {
    // Maximum length for file and directory names
    FS_MAX_NAME_LENGTH = 64,
    
    // Maximum number of files/directories in a directory
    FS_MAX_FILES = 32,
    
    // Maximum directory depth
    FS_MAX_PATH_DEPTH = 8,
    
    // Memory pool size for filesystem allocations (in bytes)
    FS_MEMORY_POOL_SIZE = 32768
};

// File types
typedef enum {
    FS_TYPE_FILE,
    FS_TYPE_DIRECTORY,
    FS_TYPE_HAK_FILE  // Custom .hak file type
} FileType;

// Filesystem error codes
typedef enum {
    FS_ERROR_NONE,           // No error
    FS_ERROR_INVALID_PATH,   // Invalid path
    FS_ERROR_NOT_FOUND,      // File or directory not found
    FS_ERROR_ALREADY_EXISTS, // File or directory already exists
    FS_ERROR_OUT_OF_MEMORY,  // Out of memory
    FS_ERROR_NOT_DIRECTORY,  // Not a directory
    FS_ERROR_DIR_FULL,       // Directory is full
    FS_ERROR_NOT_IMPLEMENTED // Operation not implemented
} FileSystemError;

// Maximum file content size
#define FS_MAX_FILE_SIZE 1024

// File system entry structure
typedef struct FileSystemEntry {
    char name[FS_MAX_NAME_LENGTH];
    FileType type;
    struct FileSystemEntry* parent;
    
    // For directories: children entries
    struct FileSystemEntry* children[FS_MAX_FILES];
    size_t childCount;
    
    // For files: content storage
    union {
        // For regular files and .hak files
        struct {
            char* content;
            size_t contentSize;
        } file;
    };
} FileSystemEntry;

/**
 * @class FileSystem
 * @brief A simple in-memory filesystem implementation for MrHakOS
 * 
 * This class provides basic RAM-only filesystem operations such as creating
 * directories, navigating paths, creating files, reading files, and removing
 * entries. There is no disk persistence yet.
 */
class FileSystem {
private:
    FileSystemEntry* root;             ///< Root directory of the filesystem
    FileSystemEntry* currentDirectory; ///< Current working directory
    
    /**
     * @brief Find an entry with the given name in the specified directory
     * @param name Name of the entry to find
     * @param directory Directory to search in
     * @return Pointer to the found entry, or nullptr if not found
     */
    FileSystemEntry* findEntry(const char* name, FileSystemEntry* directory);
    
    /**
     * @brief Initialize a filesystem entry with the given parameters
     * @param entry Entry to initialize
     * @param name Name of the entry
     * @param type Type of the entry (file or directory)
     * @param parent Parent directory of the entry
     */
    void initEntry(FileSystemEntry* entry, const char* name, FileType type, FileSystemEntry* parent);

    bool splitParentPath(const char* path, FileSystemEntry** parent, char* leaf, size_t leafSize);
    bool isDescendantOf(FileSystemEntry* entry, FileSystemEntry* ancestor);
    
public:
    /**
     * @brief Check if a filename has a specific extension
     * @param filename The filename to check
     * @param extension The extension to check for (including the dot)
     * @return true if the filename has the extension, false otherwise
     */
    bool hasExtension(const char* filename, const char* extension);
    
    /**
     * @brief Constructor
     */
    FileSystem();
    
    /**
     * @brief Initialize the filesystem
     * Creates the root directory and sets it as the current directory
     */
    void init();
    
    /**
     * @brief Create a new directory in the current directory
     * @param name Name of the directory to create
     * @return true if successful, false otherwise
     */
    bool mkdir(const char* name);
    
    /**
     * @brief Change the current directory
     * @param path Path to change to (can be absolute or relative)
     * @return true if successful, false otherwise
     */
    bool cd(const char* path);
    
    /**
     * @brief List the contents of the current directory
     * This is used by the terminal's ls command
     */
    void ls();

    /**
     * @brief Resolve an absolute or relative path to an entry
     * @param path Path to resolve. Supports /, ., .., and slash-separated names.
     * @return Pointer to the entry, or nullptr if not found
     */
    FileSystemEntry* resolvePath(const char* path);

    /**
     * @brief Create or overwrite a regular file with content
     * @param path File path
     * @param content Null-terminated text content
     * @return true if successful, false otherwise
     */
    bool createFile(const char* path, const char* content);

    /**
     * @brief Write a regular file. Alias for createFile for shell semantics.
     */
    bool writeFile(const char* path, const char* content);

    /**
     * @brief Read a regular file or .hak file into a caller buffer
     */
    bool readFile(const char* path, char* buffer, size_t bufferSize);

    /**
     * @brief Remove a file or directory from the RAM filesystem
     * @param path Entry to remove
     * @param recursive Required for removing directories
     * @return true if successful, false otherwise
     */
    bool remove(const char* path, bool recursive);
    
    /**
     * @brief Create a new .hak file with the given content
     * @param name Name of the file to create (should end with .hak)
     * @param content Content to write to the file
     * @return true if successful, false otherwise
     */
    bool createHakFile(const char* name, const char* content);
    
    /**
     * @brief Read the content of a .hak file
     * @param name Name of the file to read
     * @param buffer Buffer to store the content
     * @param bufferSize Size of the buffer
     * @return true if successful, false otherwise
     */
    bool readHakFile(const char* name, char* buffer, size_t bufferSize);
    
    /**
     * @brief Copy a file (not implemented yet)
     * @param source Source file path
     * @param destination Destination file path
     * @return true if successful, false otherwise
     */
    bool cp(const char* source, const char* destination);
    
    /**
     * @brief Move or rename a file or directory (not implemented yet)
     * @param source Source path
     * @param destination Destination path
     * @return true if successful, false otherwise
     */
    bool mv(const char* source, const char* destination);
    
    /**
     * @brief Get the current path as a string
     * @param buffer Buffer to store the path
     * @param bufferSize Size of the buffer
     */
    void getCurrentPath(char* buffer, size_t bufferSize);
    
    /**
     * @brief Get the current directory
     * @return Pointer to the current directory entry
     */
    FileSystemEntry* getCurrentDirectory();

    /**
     * @brief Get the root directory
     */
    FileSystemEntry* getRootDirectory();
};

// Global filesystem instance
extern FileSystem* g_filesystem;

#ifdef __cplusplus
}
#endif

#endif // _LIBC_FILESYSTEM_H
