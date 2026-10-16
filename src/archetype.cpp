#include <ecs/archetype.hpp>

#include <algorithm>

namespace ecs {
namespace {
std::vector<ComponentId> dataTypes(const Signature& sig) {
    std::vector<ComponentId> out;
    for (ComponentId id : sig)
        if (!typeInfo(id).tag) out.push_back(id);
    return out;
}
}  // namespace

Archetype::Archetype(ArchetypeId id_, Signature sig)
    : id(id_), signature(std::move(sig)), columnTypes(dataTypes(signature)), table(columnTypes) {}

uint16_t columnOf(const Archetype& a, ComponentId c) noexcept {
    auto it = std::lower_bound(a.columnTypes.begin(), a.columnTypes.end(), c);
    if (it == a.columnTypes.end() || *it != c) return NoColumn;
    return static_cast<uint16_t>(it - a.columnTypes.begin());
}

Edge buildEdge(const Archetype& src, Archetype& dst, ComponentId added) {
    Edge e;
    e.target = &dst;
    e.srcToDst.assign(src.columnTypes.size(), -1);
    size_t j = 0;
    for (size_t i = 0; i < src.columnTypes.size(); ++i) {
        while (j < dst.columnTypes.size() && dst.columnTypes[j] < src.columnTypes[i]) ++j;
        if (j < dst.columnTypes.size() && dst.columnTypes[j] == src.columnTypes[i])
            e.srcToDst[i] = static_cast<int16_t>(j);
    }
    if (added != NoComponent && !typeInfo(added).tag && columnOf(src, added) == NoColumn)
        e.addedColumn = static_cast<int16_t>(columnOf(dst, added));
    return e;
}

}  // namespace ecs
