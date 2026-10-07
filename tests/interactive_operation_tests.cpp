#include "application/InteractiveOperation.h"

#include <cassert>

int main()
{
    using namespace cad::application;

    InteractiveOperationSession session;
    assert(session.beginCreate(InteractiveOperationKind::Extrude, {"sketch"}));
    assert(session.active());
    assert(session.updatePreview("distance", 35.0));
    const auto committed = session.commit();
    assert(committed.has_value());
    assert(committed->editingFeatureId.empty());
    assert(committed->parameters.at("distance") == 35.0);
    assert(!session.active());

    assert(session.beginEdit(InteractiveOperationKind::PushPull, "pushpull",
                             {{"distance", 20.0}}));
    assert(session.context().editingExisting());
    assert(session.context().originalParameters.at("distance") == 20.0);
    assert(session.updatePreview("distance", 35.0));
    session.cancel();
    assert(!session.active());
    assert(!session.commit().has_value());
    return 0;
}
