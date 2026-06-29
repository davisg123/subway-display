import os

Import("env")

# Inject secrets into the binary at build time so they never live in committed
# source. The dashboard page (config_portal.cpp) carries a %GOOGLE_MAPS_API_KEY%
# runtime placeholder that is replaced from this macro when the page is served.
#
# The key lives only in web_dev/.env (gitignored). Note: a Google Maps JS API
# key is sent to the browser by design, so restrict it (HTTP referrer / API
# restrictions) in the Google Cloud Console — that, not secrecy, is the control.
def load_env(path):
    out = {}
    if os.path.exists(path):
        with open(path) as f:
            for line in f:
                line = line.strip()
                if line and not line.startswith("#") and "=" in line:
                    k, v = line.split("=", 1)
                    out[k.strip()] = v.strip()
    return out


# Prefer an environment variable (set by the release workflow from a GitHub
# Actions secret); fall back to web_dev/.env for local builds.
env_path = os.path.join(env.subst("$PROJECT_DIR"), "web_dev", ".env")
secrets = load_env(env_path)
key = os.environ.get("GOOGLE_MAPS_API_KEY") or secrets.get("GOOGLE_MAPS_API_KEY", "")
if not key:
    print("WARNING: GOOGLE_MAPS_API_KEY not set (env or web_dev/.env) — Maps will not load")

env.Append(CPPDEFINES=[("GOOGLE_MAPS_API_KEY", env.StringifyMacro(key))])
