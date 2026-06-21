#pragma once

/**
 * ApolloCore - Binary Embedding Storage (.fbin format)
 * 
 * Efficient binary format for storing float32 embeddings on disk.
 * 
 * File Format:
 *   Header (16 bytes):
 *     - Magic: "FBIN" (4 bytes)
 *     - Version: uint32 (4 bytes) - format version
 *     - Dimensions: uint32 (4 bytes) - vector dimensions
 *     - Reserved: 4 bytes - for future use
 * 
 *   Data:
 *     - float32[dimensions] - the embedding values
 * 
 * Features:
 *   - Compact: 4 bytes per float + 16 byte header
 *   - Fast: Direct memory mapping
 *   - Versioned: Backwards compatible
 *   - Standard: Interoperable with other tools
 */

#include <string>
#include <vector>
#include <cstdint>
#include <cstddef>

namespace apollocore_v2 {

// ============================================================================
// FBIN Constants
// ============================================================================

constexpr char FBIN_MAGIC[5] = "FBIN";      // Including null terminator
constexpr uint32_t FBIN_VERSION = 1;
constexpr size_t FBIN_HEADER_SIZE = 16;

// ============================================================================
// FBIN Functions
// ============================================================================

/**
 * Get the file size for given dimensions
 */
inline size_t fbin_file_size(uint32_t dimensions) {
    return FBIN_HEADER_SIZE + dimensions * sizeof(float);
}

/**
 * Save a single embedding to a .fbin file
 * @param path Output file path
 * @param embedding Vector of floats (must be non-empty)
 * @return true on success
 */
bool save_fbin(const std::string& path, const std::vector<float>& embedding);

/**
 * Save a single embedding to a .fbin file (pointer version)
 * @param path Output file path
 * @param data Pointer to float array
 * @param dimensions Number of dimensions
 * @return true on success
 */
bool save_fbin(const std::string& path, const float* data, size_t dimensions);

/**
 * Load an embedding from a .fbin file
 * @param path Input file path
 * @param expected_dims Expected dimensions (0 = any)
 * @return Vector of floats (empty on error)
 */
std::vector<float> load_fbin(const std::string& path, uint32_t expected_dims = 0);

/**
 * Load embedding directly into a pre-allocated buffer
 * @param path Input file path
 * @param buffer Pre-allocated buffer
 * @param buffer_size Size of buffer
 * @return Number of floats read, 0 on error
 */
size_t load_fbin_into(const std::string& path, float* buffer, size_t buffer_size);

/**
 * Get dimensions from a .fbin file (reads only header)
 * @param path File path
 * @return Dimensions (0 on error)
 */
uint32_t get_fbin_dimensions(const std::string& path);

/**
 * Verify a file is a valid .fbin file
 * @param path File path
 * @return true if valid
 */
bool is_valid_fbin(const std::string& path);

/**
 * Memory-map a .fbin file for fast random access
 * @param path File path
 * @return Mapped data pointer, nullptr on error
 * 
 * Note: Call unmap_fbin when done.
 */
const float* mmap_fbin(const std::string& path, size_t& out_size);

/**
 * Unmap a previously mapped .fbin file
 * @param data Pointer returned by mmap_fbin
 * @param size Size of mapped data
 */
void unmap_fbin(const float* data, size_t size);

} // namespace apollocore_v2
