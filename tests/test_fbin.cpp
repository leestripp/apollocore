/**
 * ApolloCore - FBIN (Binary Embedding Storage) Tests
 */

// CRITICAL: Undefine NDEBUG to enable assert() in Release mode
#undef NDEBUG

#include "apollocore/fbin.hpp"
#include <iostream>
#include <cassert>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <vector>

using namespace apollocore_v2;

const std::string TEST_DIR = "/tmp/apollo_fbin_tests/";

void cleanup() {
    if (std::filesystem::exists(TEST_DIR)) {
        std::filesystem::remove_all(TEST_DIR);
    }
    std::filesystem::create_directories(TEST_DIR);
}

void test_save_load_vector() {
    std::cout << "Test: Save/Load Vector... ";
    
    std::vector<float> embedding = {0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f};
    std::string path = TEST_DIR + "test1.fbin";
    
    assert(save_fbin(path, embedding));
    
    auto loaded = load_fbin(path);
    assert(!loaded.empty());
    assert(loaded.size() == embedding.size());
    
    for (size_t i = 0; i < embedding.size(); i++) {
        assert(std::abs(loaded[i] - embedding[i]) < 1e-6f);
    }
    
    std::cout << "PASSED\n";
}

void test_save_load_pointer() {
    std::cout << "Test: Save/Load Pointer... ";
    
    float data[] = {1.0f, 2.0f, 3.0f, 4.0f};
    std::string path = TEST_DIR + "test2.fbin";
    
    assert(save_fbin(path, data, 4));
    
    auto loaded = load_fbin(path);
    assert(loaded.size() == 4);
    assert(std::abs(loaded[0] - 1.0f) < 1e-6f);
    assert(std::abs(loaded[3] - 4.0f) < 1e-6f);
    
    std::cout << "PASSED\n";
}

void test_large_embedding() {
    std::cout << "Test: Large Embedding (768 dims)... ";
    
    std::vector<float> embedding(768);
    for (size_t i = 0; i < 768; i++) {
        embedding[i] = static_cast<float>(i) / 768.0f;
    }
    
    std::string path = TEST_DIR + "test_large.fbin";
    assert(save_fbin(path, embedding));
    
    auto loaded = load_fbin(path);
    assert(loaded.size() == 768);
    
    for (size_t i = 0; i < 768; i++) {
        assert(std::abs(loaded[i] - embedding[i]) < 1e-5f);
    }
    
    std::cout << "PASSED (768 dimensions)\n";
}

void test_file_size() {
    std::cout << "Test: File Size Calculation... ";
    
    size_t size_768 = fbin_file_size(768);
    size_t expected = 16 + 768 * sizeof(float);
    assert(size_768 == expected);
    
    size_t size_0 = fbin_file_size(0);
    assert(size_0 == 16);
    
    std::cout << "PASSED (" << size_768 << " bytes for 768 dims)\n";
}

void test_get_dimensions() {
    std::cout << "Test: Get Dimensions... ";
    
    std::vector<float> embedding(384, 0.5f);
    std::string path = TEST_DIR + "test_dims.fbin";
    save_fbin(path, embedding);
    
    uint32_t dims = get_fbin_dimensions(path);
    assert(dims == 384);
    
    std::cout << "PASSED (detected " << dims << " dims)\n";
}

void test_validate_fbin() {
    std::cout << "Test: Validate FBIN File... ";
    
    // Valid file
    std::vector<float> emb = {0.1f, 0.2f, 0.3f};
    std::string valid_path = TEST_DIR + "valid.fbin";
    save_fbin(valid_path, emb);
    assert(is_valid_fbin(valid_path));
    
    // Non-existent file
    assert(!is_valid_fbin(TEST_DIR + "nonexistent.fbin"));
    
    // Invalid file (random text)
    std::string invalid_path = TEST_DIR + "invalid.fbin";
    {
        std::ofstream f(invalid_path, std::ios::binary);
        const char* data = "NOT_FBIN_DATA";
        f.write(data, std::strlen(data));
    }
    assert(!is_valid_fbin(invalid_path));
    
    std::cout << "PASSED\n";
}

void test_dimension_mismatch() {
    std::cout << "Test: Dimension Mismatch... ";
    
    std::vector<float> embedding(384, 0.0f);
    std::string path = TEST_DIR + "test_mismatch.fbin";
    save_fbin(path, embedding);
    
    // Should fail because we expect 768 dims
    auto loaded = load_fbin(path, 768);
    assert(loaded.empty());
    
    // Should succeed with correct dimensions
    auto loaded_correct = load_fbin(path, 384);
    assert(!loaded_correct.empty());
    
    std::cout << "PASSED\n";
}

void test_load_into_buffer() {
    std::cout << "Test: Load Into Buffer... ";
    
    std::vector<float> embedding = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
    std::string path = TEST_DIR + "test_buffer.fbin";
    save_fbin(path, embedding);
    
    float buffer[10] = {0};
    size_t read = load_fbin_into(path, buffer, 10);
    assert(read == 5);
    assert(std::abs(buffer[0] - 1.0f) < 1e-6f);
    assert(std::abs(buffer[4] - 5.0f) < 1e-6f);
    
    // Test with too-small buffer
    float small_buffer[3] = {0};
    size_t read_small = load_fbin_into(path, small_buffer, 3);
    assert(read_small == 0);
    
    std::cout << "PASSED\n";
}

void test_mmap_load() {
    std::cout << "Test: Memory-Mapped Load... ";
    
    std::vector<float> embedding(512);
    for (size_t i = 0; i < 512; i++) {
        embedding[i] = static_cast<float>(i + 1) * 0.001f;
    }
    
    std::string path = TEST_DIR + "test_mmap.fbin";
    save_fbin(path, embedding);
    
    size_t mapped_size = 0;
    const float* data = mmap_fbin(path, mapped_size);
    assert(data != nullptr);
    assert(mapped_size == 512 * sizeof(float));
    
    // Read first few values
    for (size_t i = 0; i < 10; i++) {
        assert(std::abs(data[i] - embedding[i]) < 1e-5f);
    }
    
    unmap_fbin(data, mapped_size);
    
    std::cout << "PASSED\n";
}

void test_special_values() {
    std::cout << "Test: Special Float Values... ";
    
    std::vector<float> embedding = {
        0.0f, 1.0f, -1.0f, 
        std::numeric_limits<float>::min(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::epsilon()
    };
    
    std::string path = TEST_DIR + "test_special.fbin";
    save_fbin(path, embedding);
    
    auto loaded = load_fbin(path);
    assert(loaded.size() == embedding.size());
    assert(loaded[0] == 0.0f);
    assert(loaded[1] == 1.0f);
    assert(loaded[2] == -1.0f);
    
    std::cout << "PASSED\n";
}

void test_empty_embedding() {
    std::cout << "Test: Empty Embedding Rejected... ";
    
    std::vector<float> empty;
    std::string path = TEST_DIR + "empty.fbin";
    
    // Should fail gracefully
    assert(!save_fbin(path, empty));
    
    std::cout << "PASSED\n";
}

void test_realistic_workflow() {
    std::cout << "Test: Realistic Workflow... ";
    
    // Simulate saving multiple chunk embeddings
    const size_t num_chunks = 100;
    const size_t dims = 768;
    
    for (size_t i = 0; i < num_chunks; i++) {
        std::vector<float> embedding(dims);
        for (size_t d = 0; d < dims; d++) {
            embedding[d] = static_cast<float>(i * dims + d) * 0.0001f;
        }
        
        std::string path = TEST_DIR + "chunk_" + std::to_string(i) + ".fbin";
        assert(save_fbin(path, embedding));
    }
    
    // Verify all files
    for (size_t i = 0; i < num_chunks; i++) {
        std::string path = TEST_DIR + "chunk_" + std::to_string(i) + ".fbin";
        assert(is_valid_fbin(path));
        
        auto loaded = load_fbin(path);
        assert(loaded.size() == dims);
    }
    
    std::cout << "PASSED (" << num_chunks << " files saved & loaded)\n";
}

int main() {
    std::cout << "\n";
    std::cout << "╔═══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║       ApolloCore - FBIN (Binary Embedding) Tests         ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════════════╝\n";
    std::cout << "\n";
    
    cleanup();
    
    test_save_load_vector();
    
    // Verify file exists
    if (std::filesystem::exists(TEST_DIR + "test1.fbin")) {
        auto size = std::filesystem::file_size(TEST_DIR + "test1.fbin");
        std::cout << "\n[DEBUG] test1.fbin size: " << size << " bytes (expected: 48)\n";
    } else {
        std::cout << "\n[DEBUG] test1.fbin DOES NOT EXIST!\n";
    }
    
    test_save_load_pointer();
    test_large_embedding();
    test_file_size();
    test_get_dimensions();
    test_validate_fbin();
    test_dimension_mismatch();
    test_load_into_buffer();
    test_mmap_load();
    test_special_values();
    test_empty_embedding();
    test_realistic_workflow();
    
    std::cout << "\nFiles in test dir:\n";
    for (const auto& entry : std::filesystem::directory_iterator(TEST_DIR)) {
        std::cout << "  " << entry.path().filename() << " (" << entry.file_size() << " bytes)\n";
    }
    // Don't cleanup - let user inspect files
    
    std::cout << "\n";
    std::cout << "╔═══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  ✅ All FBIN Tests PASSED                                ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════════════╝\n";
    std::cout << "\n";
    std::cout << "📋 Summary:\n";
    std::cout << "   • Save/Load vector: PASSED\n";
    std::cout << "   • Save/Load pointer: PASSED\n";
    std::cout << "   • Large embedding (768d): PASSED\n";
    std::cout << "   • File size calculation: PASSED\n";
    std::cout << "   • Get dimensions: PASSED\n";
    std::cout << "   • Validate FBIN: PASSED\n";
    std::cout << "   • Dimension mismatch handling: PASSED\n";
    std::cout << "   • Load into buffer: PASSED\n";
    std::cout << "   • Memory-mapped I/O: PASSED\n";
    std::cout << "   • Special float values: PASSED\n";
    std::cout << "   • Empty embedding rejected: PASSED\n";
    std::cout << "   • Realistic workflow (100 files): PASSED\n";
    std::cout << "\n";
    std::cout << "💾 File Format:\n";
    std::cout << "   Header (16 bytes):\n";
    std::cout << "     - Magic: \"FBIN\" (4 bytes)\n";
    std::cout << "     - Version: uint32 (4 bytes)\n";
    std::cout << "     - Dimensions: uint32 (4 bytes)\n";
    std::cout << "     - Reserved: 4 bytes\n";
    std::cout << "   Data:\n";
    std::cout << "     - float32[dimensions]\n";
    std::cout << "\n";
    std::cout << "   Example: 768-dim embedding = " 
              << (16 + 768 * 4) << " bytes\n";
    std::cout << "\n";
    
    return 0;
}
