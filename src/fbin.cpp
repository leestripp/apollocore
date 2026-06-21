/**
 * ApolloCore - Binary Embedding Storage Implementation
 */

#include "apollocore/fbin.hpp"
#include <cstring>
#include <fstream>
#include <iostream>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

namespace apollocore_v2 {

// ============================================================================
// Internal Header Structure
// ============================================================================

#pragma pack(push, 1)
struct FbinHeader {
    char magic[4];
    uint32_t version;
    uint32_t dimensions;
    uint32_t reserved;
};
#pragma pack(pop)

static_assert(sizeof(FbinHeader) == FBIN_HEADER_SIZE, "Header must be 16 bytes");

// ============================================================================
// Internal Helpers
// ============================================================================

static bool read_header(const std::string& path, FbinHeader& header) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    
    file.read(reinterpret_cast<char*>(&header), sizeof(FbinHeader));
    if (file.gcount() != sizeof(FbinHeader)) {
        return false;
    }
    
    return true;
}

static bool write_header(std::ofstream& file, uint32_t dimensions) {
    FbinHeader header;
    std::memcpy(header.magic, FBIN_MAGIC, 4);
    header.version = FBIN_VERSION;
    header.dimensions = dimensions;
    header.reserved = 0;
    
    file.write(reinterpret_cast<const char*>(&header), sizeof(FbinHeader));
    return file.good();
}

// ============================================================================
// Save Functions
// ============================================================================

bool save_fbin(const std::string& path, const std::vector<float>& embedding) {
    if (embedding.empty()) {
        std::cerr << "[FBIN] Cannot save empty embedding\n";
        return false;
    }
    return save_fbin(path, embedding.data(), embedding.size());
}

bool save_fbin(const std::string& path, const float* data, size_t dimensions) {
    if (!data || dimensions == 0) {
        std::cerr << "[FBIN] Invalid data or dimensions\n";
        return false;
    }
    
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        std::cerr << "[FBIN] Cannot open file for writing: " << path << "\n";
        return false;
    }
    
    if (!write_header(file, static_cast<uint32_t>(dimensions))) {
        std::cerr << "[FBIN] Failed to write header\n";
        return false;
    }
    
    file.write(reinterpret_cast<const char*>(data), dimensions * sizeof(float));
    
    if (!file.good()) {
        std::cerr << "[FBIN] Failed to write data\n";
        return false;
    }
    
    return true;
}

// ============================================================================
// Load Functions
// ============================================================================

std::vector<float> load_fbin(const std::string& path, uint32_t expected_dims) {
    FbinHeader header;
    if (!read_header(path, header)) {
        std::cerr << "[FBIN] Cannot read header from: " << path << "\n";
        return {};
    }
    
    if (std::memcmp(header.magic, FBIN_MAGIC, 4) != 0) {
        std::cerr << "[FBIN] Invalid magic in: " << path << "\n";
        return {};
    }
    
    if (header.version != FBIN_VERSION) {
        std::cerr << "[FBIN] Unsupported version: " << header.version << "\n";
        return {};
    }
    
    if (expected_dims > 0 && header.dimensions != expected_dims) {
        std::cerr << "[FBIN] Dimension mismatch: expected " << expected_dims 
                  << " got " << header.dimensions << "\n";
        return {};
    }
    
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return {};
    }
    
    file.seekg(FBIN_HEADER_SIZE);
    
    std::vector<float> embedding(header.dimensions);
    file.read(reinterpret_cast<char*>(embedding.data()), 
              header.dimensions * sizeof(float));
    
    if (file.gcount() != static_cast<std::streamsize>(header.dimensions * sizeof(float))) {
        std::cerr << "[FBIN] Read error: incomplete data\n";
        return {};
    }
    
    return embedding;
}

size_t load_fbin_into(const std::string& path, float* buffer, size_t buffer_size) {
    if (!buffer || buffer_size == 0) return 0;
    
    FbinHeader header;
    if (!read_header(path, header)) return 0;
    
    if (std::memcmp(header.magic, FBIN_MAGIC, 4) != 0) return 0;
    if (header.dimensions > buffer_size) return 0;
    
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return 0;
    
    file.seekg(FBIN_HEADER_SIZE);
    file.read(reinterpret_cast<char*>(buffer), header.dimensions * sizeof(float));
    
    return header.dimensions;
}

// ============================================================================
// Utility Functions
// ============================================================================

uint32_t get_fbin_dimensions(const std::string& path) {
    FbinHeader header;
    if (!read_header(path, header)) return 0;
    if (std::memcmp(header.magic, FBIN_MAGIC, 4) != 0) return 0;
    return header.dimensions;
}

bool is_valid_fbin(const std::string& path) {
    FbinHeader header;
    if (!read_header(path, header)) return false;
    if (std::memcmp(header.magic, FBIN_MAGIC, 4) != 0) return false;
    if (header.version != FBIN_VERSION) return false;
    if (header.dimensions == 0) return false;
    return true;
}

// ============================================================================
// Memory-Mapped I/O
// ============================================================================

const float* mmap_fbin(const std::string& path, size_t& out_size) {
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        std::cerr << "[FBIN] Cannot open for mmap: " << path << "\n";
        return nullptr;
    }
    
    struct stat st;
    if (fstat(fd, &st) < 0) {
        close(fd);
        return nullptr;
    }
    
    if (st.st_size < static_cast<off_t>(FBIN_HEADER_SIZE)) {
        close(fd);
        return nullptr;
    }
    
    void* mapped = mmap(nullptr, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);  // Can close fd after mmap
    
    if (mapped == MAP_FAILED) {
        std::cerr << "[FBIN] mmap failed for: " << path << "\n";
        return nullptr;
    }
    
    // Verify header
    const FbinHeader* header = reinterpret_cast<const FbinHeader*>(mapped);
    if (std::memcmp(header->magic, FBIN_MAGIC, 4) != 0) {
        munmap(mapped, st.st_size);
        return nullptr;
    }
    
    // Return pointer to data (after header)
    const float* data = reinterpret_cast<const float*>(
        static_cast<const char*>(mapped) + FBIN_HEADER_SIZE
    );
    out_size = static_cast<size_t>(st.st_size) - FBIN_HEADER_SIZE;
    
    return data;
}

void unmap_fbin(const float* data, size_t size) {
    if (!data) return;
    
    // Calculate original mapped address
    const char* mapped = reinterpret_cast<const char*>(data) - FBIN_HEADER_SIZE;
    size_t total_size = size + FBIN_HEADER_SIZE;
    munmap(const_cast<char*>(mapped), total_size);
}

} // namespace apollocore_v2
