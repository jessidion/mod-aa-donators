#include "CoaPluginApi.h"

void Addmod_aa_donatorsScripts();

namespace
{
    bool Register() { Addmod_aa_donatorsScripts(); return true; }
    bool Startup() { return true; }
    void Stopping() { }
    bool Drain() { return true; }
    void Finalize() { }
}

COA_PLUGIN_DEFINE("mod_aa_donators", "0.1.0", Register, Startup, Stopping, Drain, Finalize)
