#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <unordered_map>
#include <vector>

#include "transform_utils.hpp"

template <std::size_t N>
class GeoMap {
public:
    GeoMap() noexcept = default;
    ~GeoMap() noexcept = default;

    void write(const std::array<std::uint32_t, 2> &key, const Geo &geo) noexcept {
        std::uint32_t id, offset;
        calc_block_pos(key[1], id, offset);

        map[key[0]][id][offset] = geo;
    }

    [[nodiscard]] Geo *read(const std::array<std::uint32_t, 2> &key) noexcept {
        std::uint32_t id, offset;
        calc_block_pos(key[1], id, offset);

        if (auto block = get_block(key[0], id); block && (*block)[offset].get_flag())
            return &(*block)[offset];
        else
            return nullptr;
    }

    void clear() noexcept { MapData{}.swap(map); }
    void clear(std::uint32_t match_bits, std::uint32_t mask) noexcept {
        std::vector<std::uint32_t> keys_to_erase;

        for (auto &[key1, block_map] : map) {
            if ((key1 & mask) != match_bits)
                continue;

            BlockMap{}.swap(block_map);
            keys_to_erase.push_back(key1);
        }

        for (std::uint32_t key1 : keys_to_erase) map.erase(key1);
    }

    [[nodiscard]] bool has_key0(std::uint32_t key0) const noexcept { return map.find(key0) != map.end(); }

private:
    using BlockMap = std::map<std::uint32_t, std::array<Geo, N>>;
    using MapData = std::unordered_map<std::uint32_t, BlockMap>;
    MapData map{};

    void calc_block_pos(std::uint32_t v, std::uint32_t &id, std::uint32_t &offset) const {
        id = v / N;
        offset = v % N;
    }

    std::array<Geo, N> *get_block(std::uint32_t key, std::uint32_t id) {
        auto it_key = map.find(key);
        if (it_key == map.end())
            return nullptr;

        auto &block_map = it_key->second;
        auto it_block = block_map.find(id);
        if (it_block == block_map.end())
            return nullptr;

        return &it_block->second;
    }
};
