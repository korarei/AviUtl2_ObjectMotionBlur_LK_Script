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
    constexpr GeoMap() noexcept = default;
    constexpr ~GeoMap() noexcept = default;

    constexpr void resize(int id, int idx, int num, int mode) {
        if (mode == 0) {
            clear(id);
            return;
        }

        auto &chunk = storage[id];
        std::size_t size = chunk.size();

        if (num < size) {
            std::vector<Block> temp(chunk.begin(), chunk.begin() + num);
            chunk.swap(temp);
        } else if (num > size) {
            chunk.resize(num);
        }

        auto &block = chunk.at(idx);

        if (mode == 1 || block.size() == 1)
            return;

        if (auto it = block.find(0); it != block.end()) {
            auto node = block.extract(it);
            Block{}.swap(block);
            block.insert(std::move(node));
        }
    }

    constexpr void write(int id, int idx, int pos, const Geo &geo) noexcept {
        const auto [key, offset] = decode(pos);

        auto it = storage.find(id);
        if (it == storage.end())
            return;

        auto &chunk = it->second;
        if (idx >= chunk.size())
            return;

        auto &unit = chunk[idx][key];

        if (unit[offset].is_cached(geo))
            return;

        unit[offset] = geo;
    }

    constexpr void overwrite(int id, int idx, int pos, const Geo &geo) noexcept {
        const auto [key, offset] = decode(pos);

        auto it = storage.find(id);
        if (it == storage.end())
            return;

        auto &chunk = it->second;
        if (idx >= chunk.size())
            return;

        auto &unit = chunk[idx][key];

        unit[offset] = geo;
    }

    [[nodiscard]] constexpr const Geo *read(int id, int idx, int pos) const noexcept {
        const auto [key, offset] = decode(pos);

        if (auto unit = fetch(id, idx, key); unit && (*unit)[offset].is_valid())
            return &(*unit)[offset];
        else
            return nullptr;
    }

    constexpr void clear() noexcept { Storage{}.swap(storage); }

    constexpr void clear(int id) noexcept {
        auto it_id = storage.find(id);
        if (it_id == storage.end())
            return;

        auto &chunk = it_id->second;
        std::vector<Block>{}.swap(chunk);
        storage.erase(id);
    }

private:
    using Unit = std::array<Geo, N>;
    using Block = std::map<int, Unit>;
    using Storage = std::unordered_map<int, std::vector<Block>>;
    Storage storage{};

    [[nodiscard]] constexpr std::array<int, 2> decode(int pos) const noexcept {
        const int size = static_cast<int>(N);
        return {pos / size, pos % size};
    }

    [[nodiscard]] constexpr const Unit *fetch(int id, int idx, int key) const noexcept {
        auto it_id = storage.find(id);
        if (it_id == storage.end())
            return nullptr;

        auto &chunk = it_id->second;
        if (idx >= chunk.size())
            return nullptr;

        auto &block = chunk[idx];
        auto it_key = block.find(key);
        if (it_key == block.end())
            return nullptr;

        return &it_key->second;
    }
};
