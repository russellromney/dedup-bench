#include "chunking_common.hpp"
#include "mincdc_chunking.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

bool disable_hashing = true;

extern "C" size_t mothcdc_next_chunk(const uint8_t *, size_t len, size_t,
                                      size_t, int, size_t *repeats_out) {
    if (repeats_out != nullptr) {
        *repeats_out = 0;
    }
    return std::min<std::size_t>(len, 2);
}

namespace {

class TestHashing final : public Hashing_Technique {
   public:
    void hash_chunk(File_Chunk &chunk) override {
        chunk.init_hash(HashingTech::MD5, 2);
        chunk.get_hash()[0] = static_cast<unsigned char>(chunk.get_data()[0]);
        chunk.get_hash()[1] = static_cast<unsigned char>(chunk.get_size());
    }
};

void enable_test_hashing(Chunking_Technique &chunker) {
    chunker.hash_method = std::make_unique<TestHashing>();
}

class TestChunker final : public Chunking_Technique {
   public:
    TestChunker(bool coalesce, std::size_t chunk_size,
                std::size_t read_fragment_size)
        : chunk_size_(chunk_size), read_fragment_size_(read_fragment_size) {
        coalesce_identical_metadata_records(coalesce);
    }

    uint64_t find_cutpoint(char *, uint64_t size) override { return size; }

    void chunk_stream(std::vector<std::string> &hashes,
                      std::istream &stream) override {
        std::vector<char> pending;
        std::vector<char> fragment(read_fragment_size_);

        while (stream.read(fragment.data(), fragment.size()) ||
               stream.gcount() > 0) {
            pending.insert(pending.end(), fragment.begin(),
                           fragment.begin() + stream.gcount());
            while (pending.size() >= chunk_size_) {
                create_chunk(hashes, pending.data(), chunk_size_);
                pending.erase(pending.begin(), pending.begin() + chunk_size_);
            }
        }

        if (!pending.empty()) {
            create_chunk(hashes, pending.data(), pending.size());
        }
    }

   private:
    std::size_t chunk_size_;
    std::size_t read_fragment_size_;
};

std::filesystem::path write_test_file(const std::string &name,
                                      const std::string &contents) {
    const auto path = std::filesystem::temp_directory_path() /
                      ("dedup_metadata_" + name);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(contents.data(), contents.size());
    out.close();
    return path;
}

std::filesystem::path write_mincdc_config(const std::string &name,
                                          bool caterpillar) {
    const auto path = std::filesystem::temp_directory_path() /
                      ("dedup_metadata_" + name + ".conf");
    std::ofstream out(path, std::ios::trunc);
    out << "mincdc_min_block_size=2\n"
        << "mincdc_max_block_size=2\n"
        << "mincdc_caterpillar=" << (caterpillar ? "true" : "false")
        << '\n'
        << "buffer_size=4\n";
    out.close();
    return path;
}

void generic_chunkers_count_every_chunk() {
    const auto path = write_test_file("generic", "aaaaaaaaXYXY");
    TestChunker chunker(false, 2, 3);
    const auto hashes = chunker.chunk_file(path.string());

    assert(hashes.size() == 6);
    assert(chunker.get_file_metadata_records() == hashes.size());
    std::filesystem::remove(path);
}

void caterpillar_coalesces_only_maximal_identical_runs() {
    const auto path = write_test_file("runs", "aaaaaaaaXYXY");
    TestChunker chunker(true, 2, 3);
    const auto hashes = chunker.chunk_file(path.string());

    assert(hashes.size() == 6);
    assert(chunker.get_file_metadata_records() == 2);
    std::filesystem::remove(path);

    // Equal byte prefixes do not coalesce when chunk lengths differ.
    const auto tail_path = write_test_file("length", "aaaaa");
    const auto tail_hashes = chunker.chunk_file(tail_path.string());
    assert(tail_hashes.size() == 3);
    assert(chunker.get_file_metadata_records() == 2);
    std::filesystem::remove(tail_path);
}

void runs_continue_across_read_fragment_boundaries() {
    const auto path = write_test_file("fragmented", "aaaaaaaaaa");
    TestChunker chunker(true, 2, 1);
    const auto hashes = chunker.chunk_file(path.string());

    assert(hashes.size() == 5);
    assert(chunker.get_file_metadata_records() == 1);
    std::filesystem::remove(path);
}

void runs_reset_at_file_boundaries() {
    const auto first = write_test_file("first", "aaaaaaaa");
    const auto second = write_test_file("second", "aaaaaaaa");
    TestChunker chunker(true, 2, 1);

    const auto first_hashes = chunker.chunk_file(first.string());
    assert(first_hashes.size() == 4);
    assert(chunker.get_file_metadata_records() == 1);

    const auto second_hashes = chunker.chunk_file(second.string());
    assert(second_hashes.size() == 4);
    assert(chunker.get_file_metadata_records() == 1);

    std::filesystem::remove(first);
    std::filesystem::remove(second);
}

void metadata_mode_does_not_change_emitted_chunks() {
    const auto path = write_test_file("unchanged", "aaaaaaaaXYXYz");
    TestChunker generic(false, 2, 1);
    TestChunker caterpillar(true, 2, 5);
    enable_test_hashing(generic);
    enable_test_hashing(caterpillar);
    disable_hashing = false;

    const auto generic_hashes = generic.chunk_file(path.string());
    const auto caterpillar_hashes = caterpillar.chunk_file(path.string());
    disable_hashing = true;

    assert(generic_hashes == caterpillar_hashes);
    assert(generic_hashes.size() == 7);
    assert(generic_hashes.front() == "6102,2");
    assert(generic_hashes.back() == "7a01,1");
    assert(generic.get_file_metadata_records() == 7);
    assert(caterpillar.get_file_metadata_records() == 3);
    std::filesystem::remove(path);
}

void mincdc_config_enables_only_caterpillar_coalescing() {
    const auto input = write_test_file("mincdc", "aaaaaaaaXYXY");
    const auto plain_config_path = write_mincdc_config("plain", false);
    const auto cat_config_path = write_mincdc_config("cat", true);
    const Config plain_config(plain_config_path.string());
    const Config cat_config(cat_config_path.string());
    MinCDC_Chunking plain(plain_config);
    MinCDC_Chunking caterpillar(cat_config);
    enable_test_hashing(plain);
    enable_test_hashing(caterpillar);
    plain.stream_buffer_size = 4;
    caterpillar.stream_buffer_size = 4;
    disable_hashing = false;

    const auto plain_hashes = plain.chunk_file(input.string());
    const auto caterpillar_hashes = caterpillar.chunk_file(input.string());
    disable_hashing = true;
    assert(plain_hashes == caterpillar_hashes);
    assert(plain_hashes.size() == 6);
    assert(plain_hashes.front() == "6102,2");
    assert(plain_hashes.back() == "5802,2");
    assert(plain.get_file_metadata_records() == 6);
    assert(caterpillar.get_file_metadata_records() == 2);

    // The first run in a new file must create a fresh metadata record.
    enable_test_hashing(caterpillar);
    disable_hashing = false;
    const auto again = caterpillar.chunk_file(input.string());
    disable_hashing = true;
    assert(again == caterpillar_hashes);
    assert(caterpillar.get_file_metadata_records() == 2);

    std::filesystem::remove(input);
    std::filesystem::remove(plain_config_path);
    std::filesystem::remove(cat_config_path);
}

}  // namespace

int main() {
    generic_chunkers_count_every_chunk();
    caterpillar_coalesces_only_maximal_identical_runs();
    runs_continue_across_read_fragment_boundaries();
    runs_reset_at_file_boundaries();
    metadata_mode_does_not_change_emitted_chunks();
    mincdc_config_enables_only_caterpillar_coalescing();
}
