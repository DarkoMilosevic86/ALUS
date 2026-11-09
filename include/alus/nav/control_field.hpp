// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
//
// This file is part of ALUS (Access Linux Using Speech).
//
// Where a document's control fields come from.
//
// This is half of what 01-ARHITEKTURA.md §9.3 calls the most important new
// code in ALUS. AT-SPI hands over flat text plus attributes per offset; it
// does NOT hand over NVDA's nested `controlStart`/`controlEnd` stream, and
// every piece of document reading rests on that stream. The other half --
// turning an ancestor chain into the stream -- is `nav::AncestorChainDiff` in
// document_range.hpp. This file is the SOURCE of the fields it diffs.
//
// Two ports, one per function:
//
//   ControlFieldSynth::forObject
//       port of `compoundDocuments._getControlFieldForObject` (:164-211)
//   normaliseControlField
//       port of the CONCEPT of `virtualBuffers._normalizeControlField`
//       (:393-448): one place that turns a backend's attribute soup into
//       canonical ALUS keys.
//
// WHY THE NORMALISER STILL EXISTS when the synthesiser already produces
// canonical keys. Because it has a second caller: anything that gets fields
// from somewhere other than an `a11y::Object` -- an app module (phase 8)
// reading a toolkit's own document attributes, an imported field map. The
// rule §9.3 asks for is "ONE place that decides", and the synthesiser calling
// the normaliser on its own output is what makes that true rather than
// aspirational.
//
// THE THREE DECISIONS IN HERE THAT ARE AUDIBLE WHEN WRONG:
//
//  1. `uniqueId` IS (bus name, object path), not a counter. §9.1: AT-SPI node
//     identity is MORE stable than NVDA's own `(docHandle, ID)`. It is what
//     `commonFieldCount` compares (NVDA #2199), so an unstable one makes a
//     100-line document announce "list with 5 items" on every single line --
//     which is exactly what T6.3 measures.
//  2. A NAMED LINK DESTINATION IS NOT A LINK. `<a name="x">` has role LINK
//     and no LINKED state. Announcing it as a link sends the user hunting for
//     something that cannot be activated, so it produces no field at all.
//  3. FOUR STATES ARE DISCARDED (editable, multiline, focused, focusable).
//     They are properties of the CONTAINER the user is already in, not news
//     about the control they just entered; NVDA discards the first three and
//     the fourth is a measured addition (FAZA6.md).
//
// THREADING: main thread only -- it reads an a11y::Object.
#pragma once

#include "alus/a11y/object.hpp"
#include "alus/text/fields.hpp"

#include <optional>
#include <string>

namespace alus::nav {

// NVDA: `compoundDocuments.TreeCompoundTextInfo._isObjectEditableText`. A
// paragraph or an edit field inside a document is "basically just a text
// node": it contains the text rather than being a control worth announcing.
bool isObjectEditableText(const a11y::Object& object);

// NVDA: `_isNamedlinkDestination`. Decision 2 above.
bool isNamedLinkDestination(const a11y::Object& object);

// The canonical form. Called by the synthesiser on its own output, and by
// anything else that produces fields.
//
// What it guarantees to whoever reads a field afterwards:
//   * `table-*` keys are present as DECIMAL INTEGERS or absent -- never "" and
//     never "1" meaning true;
//   * `isHidden` is the typed member, set from whichever of the four spellings
//     the toolkit used (`hidden`, `invisible`, ATK's own; N46);
//   * `alwaysReportName` is set for a landmark and a region, because an
//     unnamed one is useless and a named one IS its name;
//   * a self-referential row/column header is removed -- a header cell whose
//     own header is itself, which Chrome emits and which would otherwise be
//     said twice.
void normaliseControlField(text::ControlField& field);

class ControlFieldSynth {
public:
    // The settings decide nothing here; they are taken because a field is
    // useless without knowing whether layout tables count, and that is the one
    // judgement the synthesiser cannot defer to the presentation layer: a
    // layout table has to be MARKED at the source, where the attributes are.
    explicit ControlFieldSynth(const text::FormatReportSettings& settings);

    // NVDA: `_getControlFieldForObject`. Nothing when the object is not worth
    // a field at all -- a text node, a named link destination, or a dead
    // object.
    //
    // `ignoreEditableText` false is what the embedded-object path passes: at a
    // U+FFFC the field IS the object, even when it is an edit field.
    std::optional<text::ControlField> forObject(const a11y::Object& object,
                                                bool ignoreEditableText = true) const;

    // The identity §9.1 specifies: (bus name, object path). Public because
    // T6.4 measures its stability directly, and because the tree interceptor
    // compares roots with it.
    static std::string uniqueIdFor(const a11y::Object& object);

private:
    const text::FormatReportSettings& settings_;
};

}  // namespace alus::nav
