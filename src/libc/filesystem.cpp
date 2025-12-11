#include <filesystem.hpp>
#include <string.hpp>

// Custom implementation of operator new for our freestanding environment
void* operator new(size_t size) {
    // In a real OS, this would allocate memory from the heap
    // For now, we'll use a static buffer as a simple memory pool
    static char memory_pool[FS_MEMORY_POOL_SIZE];
    static size_t next_free = 0;
    // Debug: mark allocation activity
    volatile unsigned short* vga_dbg = (unsigned short*)0xB8000;
    vga_dbg[41] = 0x1F61; // 'a' indicates operator new called
    
    // Align allocation to 16 bytes for 64-bit safety
    next_free = (next_free + 15) & ~((size_t)15);
    // Check if we have enough space
    if (next_free + size > sizeof(memory_pool)) {
        // Out of memory
        return nullptr;
    }
    
    // Allocate memory from our pool
    void* ptr = &memory_pool[next_free];
    next_free += size;
    
    // Align to 16-byte boundary for next allocation
    next_free = (next_free + 15) & ~((size_t)15);
    
    return ptr;
}

// Custom implementation of operator new[] for array allocations
void* operator new[](size_t size) {
    // Reuse the same implementation as regular new
    return operator new(size);
}

// Custom implementation of operator delete
void operator delete(void* ptr) noexcept {
    // In this simple implementation, we don't actually free memory
    // A real implementation would return memory to the heap
    // This is just a placeholder to satisfy the compiler
    (void)ptr; // Unused parameter
}

// Custom implementation of operator delete with size
void operator delete(void* ptr, size_t size) noexcept {
    // Same as regular delete, we don't actually free memory
    (void)ptr;  // Unused parameter
    (void)size; // Unused parameter
}

// Custom implementation of operator delete[] for array deallocations
void operator delete[](void* ptr) noexcept {
    // Reuse the same implementation as regular delete
    operator delete(ptr);
}

// Global filesystem instance
FileSystem* g_filesystem = nullptr;

FileSystem::FileSystem() {
    // Debug: show constructor entry/exit in VGA for crash isolation
    volatile unsigned short* vga = (unsigned short*)0xB8000;
    vga[20] = 0x1F58; // 'X' marks FileSystem ctor start
    vga[22] = 0x1F78; // 'x' before root assignment
    root = nullptr;
    vga[23] = 0x1F72; // 'r' after root assignment
    currentDirectory = nullptr;
    vga[24] = 0x1F63; // 'c' after currentDirectory assignment
    vga[21] = 0x1F59; // 'Y' marks FileSystem ctor end
    vga[25] = 0x1F79; // 'y' final mark
}

void FileSystem::init() {
    // Debug: mark FileSystem init start/end
    volatile unsigned short* vga = (unsigned short*)0xB8000;
    vga[42] = 0x1F6D; // 'm' start of init
    // Allocate memory for root directory
    root = new FileSystemEntry;
    vga[43] = 0x1F31; // '1' after root allocation
    
    initEntry(root, "/", FS_TYPE_DIRECTORY, nullptr);
    vga[44] = 0x1F32; // '2' after initEntry
    
    currentDirectory = root;
    vga[45] = 0x1F33; // '3' after currentDirectory set
    
    g_filesystem = this;
    vga[46] = 0x1F34; // '4' after g_filesystem set
    vga[47] = 0x1F6E; // 'n' end of init
}

void FileSystem::initEntry(FileSystemEntry* entry, const char* name, FileType type, FileSystemEntry* parent) {
    // Debug markers to trace initEntry
    volatile unsigned short* vga = (unsigned short*)0xB8000;
    vga[48] = 0x1F49; // 'I' start initEntry
    // Copy name (with bounds checking)
    size_t nameLen = strlen(name);
    if (nameLen >= FS_MAX_NAME_LENGTH) {
        nameLen = FS_MAX_NAME_LENGTH - 1;
    }
    
    for (size_t i = 0; i < nameLen; i++) {
        entry->name[i] = name[i];
    }
    entry->name[nameLen] = '\0';
    
    // Set type and parent
    entry->type = type;
    entry->parent = parent;
    
    // Initialize directory metadata only; avoid blanket zeroing children array
    // We rely on childCount to control iteration and set children entries lazily.
    if (type == FS_TYPE_DIRECTORY) {
        entry->childCount = 0;
    }
    vga[49] = 0x1F4A; // 'J' end initEntry
}

bool FileSystem::mkdir(const char* name) {
    // Check if name is valid
    if (name == nullptr || name[0] == '\0') {
        return false;
    }
    
    // Check if directory already exists
    if (findEntry(name, currentDirectory) != nullptr) {
        return false; // Entry with this name already exists
    }
    
    // Check if current directory has space for a new entry
    if (currentDirectory->childCount >= FS_MAX_FILES) {
        return false; // Directory is full
    }
    
    // Allocate memory for new directory
    // Avoid implicit zero-initialization that may use problematic memset
    FileSystemEntry* newDir = new FileSystemEntry;
    
    // Check if memory allocation failed
    if (newDir == nullptr) {
        return false; // Out of memory
    }
    
    // Initialize new directory
    initEntry(newDir, name, FS_TYPE_DIRECTORY, currentDirectory);
    
    // Add new directory to current directory's children
    currentDirectory->children[currentDirectory->childCount] = newDir;
    currentDirectory->childCount++;
    
    return true;
}

bool FileSystem::cd(const char* path) {
    // Handle special cases
    if (strcmp(path, "/") == 0) {
        // Change to root directory
        currentDirectory = root;
        return true;
    }
    
    if (strcmp(path, ".") == 0) {
        // Stay in current directory
        return true;
    }
    
    if (strcmp(path, "..") == 0) {
        // Go up one level
        if (currentDirectory->parent != nullptr) {
            currentDirectory = currentDirectory->parent;
            return true;
        }
        return false; // Already at root
    }
    
    // Find directory in current directory
    FileSystemEntry* entry = findEntry(path, currentDirectory);
    
    if (entry != nullptr && entry->type == FS_TYPE_DIRECTORY) {
        currentDirectory = entry;
        return true;
    }
    
    return false; // Directory not found or not a directory
}

void FileSystem::ls() {
    // List all entries in current directory
    for (size_t i = 0; i < currentDirectory->childCount; i++) {
        FileSystemEntry* entry = currentDirectory->children[i];
        
        // Print entry type and name
        if (entry->type == FS_TYPE_DIRECTORY) {
            // Directory entries are handled by the terminal's cmdLs method
            // which will add the trailing slash when displaying
        } else if (entry->type == FS_TYPE_HAK_FILE) {
            // .hak files are handled by the terminal's cmdLs method
            // which will display the file name with its extension
        }
    }
}

FileSystemEntry* FileSystem::findEntry(const char* name, FileSystemEntry* directory) {
    // Search for entry with given name in the specified directory
    for (size_t i = 0; i < directory->childCount; i++) {
        if (strcmp(directory->children[i]->name, name) == 0) {
            return directory->children[i];
        }
    }
    
    return nullptr; // Entry not found
}

void FileSystem::getCurrentPath(char* buffer, size_t bufferSize) {
    if (buffer == nullptr || bufferSize == 0) {
        return;
    }
    
    // Start with empty string
    buffer[0] = '\0';
    
    // If we're at root, just return "/"
    if (currentDirectory == root) {
        if (bufferSize > 1) {
            buffer[0] = '/';
            buffer[1] = '\0';
        }
        return;
    }
    
    // Build path by traversing up to root
    FileSystemEntry* path[FS_MAX_PATH_DEPTH];
    int depth = 0;
    
    FileSystemEntry* current = currentDirectory;
    while (current != root && depth < FS_MAX_PATH_DEPTH) {
        path[depth++] = current;
        current = current->parent;
    }
    
    // Start with root
    size_t pos = 0;
    buffer[pos++] = '/';
    
    // Add path components from root to current directory
    for (int i = depth - 1; i >= 0 && pos < bufferSize - 1; i--) {
        // Add directory name
        const char* name = path[i]->name;
        size_t nameLen = strlen(name);
        
        for (size_t j = 0; j < nameLen && pos < bufferSize - 1; j++) {
            buffer[pos++] = name[j];
        }
        
        // Add separator if not the last component
        if (i > 0 && pos < bufferSize - 1) {
            buffer[pos++] = '/';
        }
    }
    
    // Null-terminate
    buffer[pos] = '\0';
}

bool FileSystem::cp(const char* source, const char* destination) {
    // Find source file in current directory
    FileSystemEntry* sourceEntry = findEntry(source, currentDirectory);
    
    // Check if source exists and is a .hak file
    if (sourceEntry == nullptr || sourceEntry->type != FS_TYPE_HAK_FILE) {
        return false; // Source not found or not a file
    }
    
    // Parse destination to see if it contains a directory path
    FileSystemEntry* targetDir = currentDirectory;
    const char* targetFilename = destination;
    
    // Check if destination contains a directory separator (/)
    const char* lastSlash = nullptr;
    for (int i = 0; destination[i] != '\0'; i++) {
        if (destination[i] == '/') {
            lastSlash = &destination[i];
        }
    }
    
    // If there's a slash, we need to navigate to the target directory
    if (lastSlash != nullptr) {
        // Extract directory name
        static char dirName[FS_MAX_NAME_LENGTH];
        int dirLen = lastSlash - destination;
        if (dirLen >= FS_MAX_NAME_LENGTH) {
            return false; // Directory name too long
        }
        
        for (int i = 0; i < dirLen; i++) {
            dirName[i] = destination[i];
        }
        dirName[dirLen] = '\0';
        
        // Find the target directory
        targetDir = findEntry(dirName, currentDirectory);
        if (targetDir == nullptr || targetDir->type != FS_TYPE_DIRECTORY) {
            return false; // Target directory not found
        }
        
        // Filename is after the slash
        targetFilename = lastSlash + 1;
    }
    
    // Check if destination already exists in target directory
    if (findEntry(targetFilename, targetDir) != nullptr) {
        return false; // Destination already exists
    }
    
    // Check if destination has .hak extension
    if (!hasExtension(targetFilename, ".hak")) {
        return false; // Only .hak files supported
    }
    
    // Save current directory and switch to target
    FileSystemEntry* savedDir = currentDirectory;
    currentDirectory = targetDir;
    
    // Create new file with copied content
    bool result = createHakFile(targetFilename, sourceEntry->file.content);
    
    // Restore current directory
    currentDirectory = savedDir;
    
    return result;
}

bool FileSystem::mv(const char* source, const char* destination) {
    // Find source entry in current directory
    FileSystemEntry* sourceEntry = findEntry(source, currentDirectory);
    
    // Check if source exists
    if (sourceEntry == nullptr) {
        return false; // Source not found
    }
    
    // Check if destination already exists
    if (findEntry(destination, currentDirectory) != nullptr) {
        return false; // Destination already exists
    }
    
    // For files, check .hak extension
    if (sourceEntry->type == FS_TYPE_HAK_FILE) {
        if (!hasExtension(destination, ".hak")) {
            return false; // Only .hak files supported
        }
    }
    
    // Rename the entry (simple name change)
    size_t nameLen = strlen(destination);
    if (nameLen >= FS_MAX_NAME_LENGTH) {
        return false; // Name too long
    }
    
    // Copy new name
    for (size_t i = 0; i < nameLen; i++) {
        sourceEntry->name[i] = destination[i];
    }
    sourceEntry->name[nameLen] = '\0';
    
    return true;
}

FileSystemEntry* FileSystem::getCurrentDirectory() {
    return currentDirectory;
}

bool FileSystem::hasExtension(const char* filename, const char* extension) {
    if (filename == nullptr || extension == nullptr) {
        return false;
    }
    
    size_t filenameLen = strlen(filename);
    size_t extensionLen = strlen(extension);
    
    // Check if filename is long enough to have the extension
    if (filenameLen <= extensionLen) {
        return false;
    }
    
    // Compare the end of the filename with the extension
    return strcmp(filename + filenameLen - extensionLen, extension) == 0;
}

bool FileSystem::createHakFile(const char* name, const char* content) {
    // Check if name is valid
    if (name == nullptr || name[0] == '\0') {
        return false;
    }
    
    // Check if file has .hak extension
    if (!hasExtension(name, ".hak")) {
        return false; // Not a .hak file
    }
    
    // Check if file already exists
    if (findEntry(name, currentDirectory) != nullptr) {
        return false; // Entry with this name already exists
    }
    
    // Check if current directory has space for a new entry
    if (currentDirectory->childCount >= FS_MAX_FILES) {
        return false; // Directory is full
    }
    
    // Allocate memory for new file
    // Avoid implicit zero-initialization that may use problematic memset
    FileSystemEntry* newFile = new FileSystemEntry;
    
    // Check if memory allocation failed
    if (newFile == nullptr) {
        return false; // Out of memory
    }
    
    // Initialize new file
    initEntry(newFile, name, FS_TYPE_HAK_FILE, currentDirectory);
    
    // Set file content
    size_t contentLen = strlen(content);
    if (contentLen > FS_MAX_FILE_SIZE - 1) {
        contentLen = FS_MAX_FILE_SIZE - 1; // Truncate if too long
    }
    
    // Allocate memory for content
    newFile->file.content = new char[contentLen + 1];
    if (newFile->file.content == nullptr) {
        // Failed to allocate memory for content
        delete newFile;
        return false;
    }
    
    // Copy content
    for (size_t i = 0; i < contentLen; i++) {
        newFile->file.content[i] = content[i];
    }
    newFile->file.content[contentLen] = '\0';
    newFile->file.contentSize = contentLen;
    
    // Add new file to current directory's children
    currentDirectory->children[currentDirectory->childCount] = newFile;
    currentDirectory->childCount++;
    
    return true;
}

bool FileSystem::readHakFile(const char* name, char* buffer, size_t bufferSize) {
    // Check parameters
    if (name == nullptr || buffer == nullptr || bufferSize == 0) {
        return false;
    }
    
    // Find file in current directory
    FileSystemEntry* entry = findEntry(name, currentDirectory);
    
    // Check if file exists and is a .hak file
    if (entry == nullptr || entry->type != FS_TYPE_HAK_FILE) {
        return false; // File not found or not a .hak file
    }
    
    // Copy content to buffer
    size_t copySize = entry->file.contentSize;
    if (copySize >= bufferSize) {
        copySize = bufferSize - 1; // Leave space for null terminator
    }
    
    for (size_t i = 0; i < copySize; i++) {
        buffer[i] = entry->file.content[i];
    }
    buffer[copySize] = '\0';
    
    return true;
}