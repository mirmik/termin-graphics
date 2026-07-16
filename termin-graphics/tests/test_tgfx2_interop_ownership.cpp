#include "guard_main.h"

#include "tgfx/tgfx2_interop.h"


TEST_CASE("tgfx2 interop device claim has one explicit owner") {
    int first_device = 0;
    int second_device = 0;
    int first_owner = 0;
    int second_owner = 0;

    REQUIRE(tgfx2_interop_get_device() == nullptr);

    CHECK(tgfx2_interop_claim_device(&first_device, &first_owner) == 1);
    CHECK(tgfx2_interop_get_device() == &first_device);

    // The owning composition root may repeat its claim during idempotent
    // initialization, but no other owner may adopt or replace it.
    CHECK(tgfx2_interop_claim_device(&first_device, &first_owner) == 1);
    CHECK(tgfx2_interop_claim_device(&first_device, &second_owner) == 0);
    CHECK(tgfx2_interop_claim_device(&second_device, &second_owner) == 0);
    CHECK(tgfx2_interop_get_device() == &first_device);

    CHECK(tgfx2_interop_release_device(&first_device, &second_owner) == 0);
    CHECK(tgfx2_interop_release_device(&second_device, &first_owner) == 0);
    CHECK(tgfx2_interop_get_device() == &first_device);

    CHECK(tgfx2_interop_release_device(&first_device, &first_owner) == 1);
    CHECK(tgfx2_interop_get_device() == nullptr);
}
