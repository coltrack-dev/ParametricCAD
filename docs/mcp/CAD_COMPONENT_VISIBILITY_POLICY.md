# Centralized Component Visibility Policy

## Root cause

Component presentation previously checked `ModelComponent::visible` (and, in
one path, `Feature::userVisible()`) independently of `VisibilityManager`.
Replacement metadata also used `visible=false` without distinguishing a
technical replacement from a user-hidden component. Consequently, components
belonging to hidden upstream Extrude/Pocket/Transform features could be
presented again, producing duplicate walls and visually filled openings.

## Policy

`VisibilityManager::effectiveComponentVisibility()` is now the single policy
entry point. The precedence is:

1. Technical-hidden or replaced components are always hidden.
2. A hidden parent Feature hides every child component.
3. Component user-hidden state hides the component.
4. Component isolation hides components outside the isolation set.
5. Feature policy or explicit component ghosting produces Ghosted state.
6. Otherwise the component is Visible.

Isolation and ghosting never resurrect technical-hidden geometry. Ghosted
components are excluded from managed selection and snapping by the viewer.
`ModelPresenter` consumes this result and never independently combines feature
and component visibility.

The persisted `technicalHidden` field is optional for backward compatibility.
Legacy files infer technical-hidden state from a non-empty replacement list.
User visibility remains in `visible`.

## Validation

The `model_tests` regression
`componentVisibilityPolicyCentralizesParentAndReplacementState` verifies
parent hiding, replacement precedence, ghosting and component isolation. Build
and CTest results are recorded with the implementation change; the currently
running user GUI is not modified by these tests.
