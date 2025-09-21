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

    void resize(std::size_t hash, std::size_t idx, std::size_t num, std::uint32_t mode) noexcept {
        if (mode == 0u) {
            clear(hash);
            return;
        }

        auto &list = map[hash];
        std::size_t size = list.size();

        if (num < size) {
            std::vector<BlockMap> temp(list.begin(), list.begin() + num);
            list.swap(temp);
        } else if (num > size) {
            list.resize(num);
        }

        auto &block_map = list[idx];

        if (auto it = block_map.find(0); mode == 2u && it != block_map.end() && block_map.size() != 1) {
            auto node = block_map.extract(it);
            BlockMap{}.swap(block_map);
            block_map.insert(std::move(node));
        }

        return;
    }

    void write(std::size_t hash, std::size_t idx, std::size_t pos, const Geo &geo) noexcept {
        const auto [id, offset] = calc_block_pos(pos);

        auto it_hash = map.find(hash);
        if (it_hash == map.end())
            return;

        auto &list = it_hash->second;
        if (idx >= list.size())
            return;

        auto &block = list[idx][id];
        block[offset] = geo;
        return;
    }

    [[nodiscard]] Geo *read(std::size_t hash, std::size_t idx, std::size_t pos) noexcept {
        const auto [id, offset] = calc_block_pos(pos);

        if (auto block = get_block(hash, idx, id); block && (*block)[offset].get_flag())
            return &(*block)[offset];
        else
            return nullptr;
    }

    void clear() noexcept { MapData{}.swap(map); }
    void clear(std::size_t hash) noexcept {
        auto it_hash = map.find(hash);
        if (it_hash == map.end())
            return;

        auto &list = it_hash->second;
        std::vector<BlockMap>{}.swap(list);
        map.erase(hash);
        return;
    }

private:
    using BlockMap = std::map<std::size_t, std::array<Geo, N>>;
    using MapData = std::unordered_map<std::size_t, std::vector<BlockMap>>;
    MapData map{};

    std::array<std::size_t, 2> calc_block_pos(std::size_t v) const { return {v / N, v % N}; }

    std::array<Geo, N> *get_block(std::size_t hash, std::size_t idx, std::size_t id) {
        auto it_hash = map.find(hash);
        if (it_hash == map.end())
            return nullptr;

        auto &list = it_hash->second;
        if (idx >= list.size())
            return nullptr;

        auto &block_map = list[idx];
        auto it_block = block_map.find(id);
        if (it_block == block_map.end())
            return nullptr;

        return &it_block->second;
    }
};
