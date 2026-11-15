#ifndef IPR_EDITOR_ADJUSTMENTTABLE_H
#define IPR_EDITOR_ADJUSTMENTTABLE_H

#include "EditRecipe.h"

#include <QString>

// The recipe's plain adjustments by name, with their ranges, in one table: the only way the
// interface reaches them. A curve, a mixer or anything with more than one value has its own command.
namespace AdjustmentTable {

enum class Written {
    Unknown,    // no adjustment of this name, or a value that is not a number
    Unchanged,  // the recipe already had this value
    Changed
};

// Bounded to the adjustment's range. A switch takes any value but zero as on.
Written write(EditRecipe::Settings &settings, const QString &name, double value);
// False for an unknown name. A switch reads 1 or 0.
bool read(const EditRecipe::Settings &settings, const QString &name, double &value);

} // namespace AdjustmentTable

#endif // IPR_EDITOR_ADJUSTMENTTABLE_H
