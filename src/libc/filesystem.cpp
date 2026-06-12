#include <filesystem.hpp>
#include <string.hpp>

static inline void hidden_vga_touch(size_t cell) {
    volatile unsigned short* vga = (unsigned short*)0xB8000;
    vga[cell] = 0x0F20;
}

// Custom implementation of operator new for our freestanding environment.
void* operator new(size_t size) {
    static char memory_pool[FS_MEMORY_POOL_SIZE];
    static size_t next_free = 0;
    hidden_vga_touch(41);

    // Align allocation to 16 bytes for 64-bit safety.
    next_free = (next_free + 15) & ~((size_t)15);

    if (next_free + size > sizeof(memory_pool)) {
        return nullptr;
    }

    void* ptr = &memory_pool[next_free];
    next_free += size;
    next_free = (next_free + 15) & ~((size_t)15);

    return ptr;
}

void* operator new[](size_t size) {
    return operator new(size);
}

// This simple bump allocator never frees individual allocations yet.
void operator delete(void* ptr) noexcept {
    (void)ptr;
}

void operator delete(void* ptr, size_t size) noexcept {
    (void)ptr;
    (void)size;
}

void operator delete[](void* ptr) noexcept {
    (void)ptr;
}

void operator delete[](void* ptr, size_t size) noexcept {
    (void)ptr;
    (void)size;
}

// Global filesystem instance
FileSystem* g_filesystem = nullptr;

FileSystem::FileSystem() {
    hidden_vga_touch(20);
    root = nullptr;
    hidden_vga_touch(22);
    currentDirectory = nullptr;
    hidden_vga_touch(23);
    hidden_vga_touch(24);
    hidden_vga_touch(21);
    hidden_vga_touch(25);
}

void FileSystem::init() {
    hidden_vga_touch(42);
    root = new FileSystemEntry;
    hidden_vga_touch(43);
    initEntry(root, "/", FS_TYPE_DIRECTORY, nullptr);
    hidden_vga_touch(44);
    currentDirectory = root;
    hidden_vga_touch(45);
    g_filesystem = this;
    hidden_vga_touch(46);
    hidden_vga_touch(47);
}

void FileSystem::initEntry(FileSystemEntry* entry, const char* name, FileType type, FileSystemEntry* parent) {
    hidden_vga_touch(48);
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
    
    // Initialize metadata only; avoid blanket zeroing because this freestanding
    // environment keeps initialization explicit.
    if (type == FS_TYPE_DIRECTORY) {
        entry->childCount = 0;
    } else {
        entry->childCount = 0;
        entry->file.content = nullptr;
        entry->file.contentSize = 0;
    }
    hidden_vga_touch(49);
}

bool FileSystem::mkdir(const char* name) {
    FileSystemEntry* parent = nullptr;
    char leaf[FS_MAX_NAME_LENGTH];
    if (!splitParentPath(name, &parent, leaf, sizeof(leaf))) {
        return false;
    }
    
    // Check if directory already exists
    if (findEntry(leaf, parent) != nullptr) {
        return false; // Entry with this name already exists
    }
    
    // Check if current directory has space for a new entry
    if (parent->childCount >= FS_MAX_FILES) {
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
    initEntry(newDir, leaf, FS_TYPE_DIRECTORY, parent);
    
    // Add new directory to current directory's children
    parent->children[parent->childCount] = newDir;
    parent->childCount++;
    
    return true;
}

bool FileSystem::cd(const char* path) {
    FileSystemEntry* entry = resolvePath(path);
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
    if (name == nullptr || directory == nullptr || directory->type != FS_TYPE_DIRECTORY) {
        return nullptr;
    }

    // Search for entry with given name in the specified directory
    for (size_t i = 0; i < directory->childCount; i++) {
        if (strcmp(directory->children[i]->name, name) == 0) {
            return directory->children[i];
        }
    }
    
    return nullptr; // Entry not found
}

FileSystemEntry* FileSystem::resolvePath(const char* path) {
    if (path == nullptr || path[0] == '\0') {
        return nullptr;
    }

    FileSystemEntry* entry = (path[0] == '/') ? root : currentDirectory;
    if (entry == nullptr) {
        return nullptr;
    }

    int i = 0;
    while (path[i] == '/') {
        i++;
    }

    if (path[i] == '\0') {
        return entry;
    }

    while (path[i] != '\0') {
        while (path[i] == '/') {
            i++;
        }
        if (path[i] == '\0') {
            break;
        }

        char component[FS_MAX_NAME_LENGTH];
        int c = 0;
        while (path[i] != '\0' && path[i] != '/') {
            if (c >= FS_MAX_NAME_LENGTH - 1) {
                return nullptr;
            }
            component[c++] = path[i++];
        }
        component[c] = '\0';

        if (strcmp(component, ".") == 0) {
            continue;
        }
        if (strcmp(component, "..") == 0) {
            if (entry->parent != nullptr) {
                entry = entry->parent;
            }
            continue;
        }
        if (entry->type != FS_TYPE_DIRECTORY) {
            return nullptr;
        }
        entry = findEntry(component, entry);
        if (entry == nullptr) {
            return nullptr;
        }
    }

    return entry;
}

bool FileSystem::splitParentPath(const char* path, FileSystemEntry** parent, char* leaf, size_t leafSize) {
    if (path == nullptr || path[0] == '\0' || parent == nullptr || leaf == nullptr || leafSize == 0) {
        return false;
    }
    if (strcmp(path, "/") == 0 || strcmp(path, ".") == 0 || strcmp(path, "..") == 0) {
        return false;
    }

    const char* lastSlash = nullptr;
    for (int i = 0; path[i] != '\0'; i++) {
        if (path[i] == '/') {
            lastSlash = &path[i];
        }
    }

    const char* leafStart = path;
    FileSystemEntry* parentDir = currentDirectory;

    if (lastSlash != nullptr) {
        leafStart = lastSlash + 1;
        if (leafStart[0] == '\0') {
            return false;
        }

        if (lastSlash == path) {
            parentDir = root;
        } else {
            char parentPath[256];
            int len = static_cast<int>(lastSlash - path);
            if (len <= 0 || len >= static_cast<int>(sizeof(parentPath))) {
                return false;
            }
            for (int i = 0; i < len; i++) {
                parentPath[i] = path[i];
            }
            parentPath[len] = '\0';
            parentDir = resolvePath(parentPath);
        }
    }

    if (parentDir == nullptr || parentDir->type != FS_TYPE_DIRECTORY) {
        return false;
    }

    size_t leafLen = strlen(leafStart);
    if (leafLen == 0 || leafLen >= leafSize || leafLen >= FS_MAX_NAME_LENGTH) {
        return false;
    }
    for (size_t i = 0; i < leafLen; i++) {
        if (leafStart[i] == '/') {
            return false;
        }
        leaf[i] = leafStart[i];
    }
    leaf[leafLen] = '\0';
    *parent = parentDir;
    return true;
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
    FileSystemEntry* sourceEntry = resolvePath(source);
    if (sourceEntry == nullptr || sourceEntry->type == FS_TYPE_DIRECTORY) {
        return false;
    }

    if (resolvePath(destination) != nullptr) {
        return false;
    }

    return createFile(destination, sourceEntry->file.content ? sourceEntry->file.content : "");
}

bool FileSystem::mv(const char* source, const char* destination) {
    FileSystemEntry* sourceParent = nullptr;
    FileSystemEntry* destParent = nullptr;
    char sourceName[FS_MAX_NAME_LENGTH];
    char destName[FS_MAX_NAME_LENGTH];

    if (!splitParentPath(source, &sourceParent, sourceName, sizeof(sourceName)) ||
        !splitParentPath(destination, &destParent, destName, sizeof(destName))) {
        return false;
    }

    FileSystemEntry* sourceEntry = findEntry(sourceName, sourceParent);
    if (sourceEntry == nullptr || findEntry(destName, destParent) != nullptr) {
        return false;
    }
    if (sourceEntry == root || isDescendantOf(destParent, sourceEntry)) {
        return false;
    }
    if (sourceParent != destParent && destParent->childCount >= FS_MAX_FILES) {
        return false;
    }

    size_t nameLen = strlen(destName);
    for (size_t i = 0; i < nameLen; i++) {
        sourceEntry->name[i] = destName[i];
    }
    sourceEntry->name[nameLen] = '\0';

    if (sourceParent != destParent) {
        size_t index = 0;
        while (index < sourceParent->childCount && sourceParent->children[index] != sourceEntry) {
            index++;
        }
        if (index >= sourceParent->childCount) {
            return false;
        }
        for (size_t i = index; i + 1 < sourceParent->childCount; i++) {
            sourceParent->children[i] = sourceParent->children[i + 1];
        }
        sourceParent->childCount--;
        sourceEntry->parent = destParent;
        destParent->children[destParent->childCount++] = sourceEntry;
    }

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

bool FileSystem::createFile(const char* path, const char* content) {
    // Check if name is valid
    if (path == nullptr || path[0] == '\0') {
        return false;
    }

    FileSystemEntry* parent = nullptr;
    char leaf[FS_MAX_NAME_LENGTH];
    if (!splitParentPath(path, &parent, leaf, sizeof(leaf))) {
        return false;
    }

    FileSystemEntry* file = findEntry(leaf, parent);
    if (file != nullptr && file->type == FS_TYPE_DIRECTORY) {
        return false;
    }

    if (file == nullptr) {
        // Check if current directory has space for a new entry.
        if (parent->childCount >= FS_MAX_FILES) {
            return false; // Directory is full.
        }

        // Allocate memory for new file.
        // Avoid implicit zero-initialization that may use problematic memset.
        file = new FileSystemEntry;
        if (file == nullptr) {
            return false; // Out of memory.
        }

        initEntry(file, leaf, hasExtension(leaf, ".hak") ? FS_TYPE_HAK_FILE : FS_TYPE_FILE, parent);
        parent->children[parent->childCount] = file;
        parent->childCount++;
    }

    // Set or replace file content. The bump allocator does not reclaim the old
    // content buffer yet, but overwriting keeps shell semantics predictable.
    if (content == nullptr) {
        content = "";
    }
    size_t contentLen = strlen(content);
    if (contentLen > FS_MAX_FILE_SIZE - 1) {
        contentLen = FS_MAX_FILE_SIZE - 1; // Truncate if too long.
    }

    char* newContent = new char[contentLen + 1];
    if (newContent == nullptr) {
        return false;
    }

    for (size_t i = 0; i < contentLen; i++) {
        newContent[i] = content[i];
    }
    newContent[contentLen] = '\0';
    file->file.content = newContent;
    file->file.contentSize = contentLen;

    return true;
}

bool FileSystem::writeFile(const char* path, const char* content) {
    return createFile(path, content);
}

bool FileSystem::readFile(const char* path, char* buffer, size_t bufferSize) {
    // Check parameters
    if (path == nullptr || buffer == nullptr || bufferSize == 0) {
        return false;
    }
    
    FileSystemEntry* entry = resolvePath(path);
    
    // Check if file exists and is readable
    if (entry == nullptr || entry->type == FS_TYPE_DIRECTORY) {
        return false;
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

bool FileSystem::createHakFile(const char* name, const char* content) {
    return createFile(name, content);
}

bool FileSystem::readHakFile(const char* name, char* buffer, size_t bufferSize) {
    return readFile(name, buffer, bufferSize);
}

bool FileSystem::isDescendantOf(FileSystemEntry* entry, FileSystemEntry* ancestor) {
    FileSystemEntry* current = entry;
    while (current != nullptr) {
        if (current == ancestor) {
            return true;
        }
        current = current->parent;
    }
    return false;
}

bool FileSystem::remove(const char* path, bool recursive) {
    FileSystemEntry* parent = nullptr;
    char leaf[FS_MAX_NAME_LENGTH];
    if (!splitParentPath(path, &parent, leaf, sizeof(leaf))) {
        return false;
    }

    FileSystemEntry* entry = findEntry(leaf, parent);
    if (entry == nullptr || entry == root) {
        return false;
    }
    if (entry->type == FS_TYPE_DIRECTORY && !recursive) {
        return false;
    }
    if (entry->type == FS_TYPE_DIRECTORY && isDescendantOf(currentDirectory, entry)) {
        return false;
    }

    size_t index = 0;
    while (index < parent->childCount && parent->children[index] != entry) {
        index++;
    }
    if (index >= parent->childCount) {
        return false;
    }
    for (size_t i = index; i + 1 < parent->childCount; i++) {
        parent->children[i] = parent->children[i + 1];
    }
    parent->childCount--;
    entry->parent = nullptr;
    return true;
}

FileSystemEntry* FileSystem::getRootDirectory() {
    return root;
}
