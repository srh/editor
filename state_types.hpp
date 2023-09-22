#ifndef QWERTILLION_STATETYPES_HPP_
#define QWERTILLION_STATETYPES_HPP_

#include <stdint.h>
#include <stddef.h>

// Certain types are defined here (for reasons such as circular reference avoidance
// between state.hpp and undo.hpp).

namespace qwi {

// This is used for _strong_ mark references -- the mark needs to get removed when the
// owning object goes away.
struct mark_id {
    // index into marks array
    size_t index = SIZE_MAX;

    // This isn't a weak ref but we still assert and exit if the assertion fails.
    uint64_t assertion_version = 0;
};

struct weak_mark_id {
    // Real version numbers start at 1, so zero means what it means.
    uint64_t version = 0;
    size_t index = SIZE_MAX;

    // Returns a temporary non-owning mark_id (which is common for copies of the owning
    // mark_id as well).  Only used after we know the version is correct.
    //
    // TODO: Make a replace_mark that takes a weak_mark_id instead.
    mark_id as_nonweak_ref() const {
        return mark_id{.index = index, .assertion_version = version};
    }
};

}  // namespace qwi

#endif // QWERTILLION_STATETYPES_HPP_
