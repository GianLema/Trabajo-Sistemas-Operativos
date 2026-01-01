#include <cspecs/cspec.h>

context (check_cspec) {
    describe("Verifica entorno CSpec") {
        it("funciona correctamente el entorno") {
            should_int(1) be equal to(1);
        } end
    } end
 } 