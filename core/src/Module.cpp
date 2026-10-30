#include "sonic/Module.h"

#include <algorithm>
#include <cmath>

namespace sonic
{

void Module::finaliseParams()
{
    values.reset (new std::atomic<float>[specs.size()]);
    for (size_t i = 0; i < specs.size(); ++i)
        values[i].store (specs[i].defaultValue);
}

int Module::paramIndex (const std::string& id) const
{
    for (size_t i = 0; i < specs.size(); ++i)
        if (specs[i].id == id)
            return (int) i;
    return -1;
}

void Module::setParam (int index, float value)
{
    if (index < 0 || index >= (int) specs.size())
        return;

    const auto& s = specs[(size_t) index];
    value = std::clamp (value, s.minValue, s.maxValue);
    if (s.isChoice())
        value = std::round (value);

    values[index].store (value, std::memory_order_relaxed);
}

float Module::getParam (int index) const
{
    if (index < 0 || index >= (int) specs.size())
        return 0.0f;
    return values[index].load (std::memory_order_relaxed);
}

} // namespace sonic
