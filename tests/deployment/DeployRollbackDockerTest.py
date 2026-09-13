#!/usr/bin/env python3
"""Real local Compose rollback exercise with an isolated database and project.

Requires Docker, PyYAML, and an already built httpserver:production-candidate.
The registry pull is bypassed for locally built fixture tags. No public ports,
production project, or production data are used; the test removes its resources.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
import uuid
from pathlib import Path

import yaml


REPO = Path(__file__).resolve().parents[2]


def command(args, *, env=None, stdin=None, timeout=120, check=True):
    result = subprocess.run(args, input=stdin, env=env, text=True,
                            capture_output=True, timeout=timeout)
    if check and result.returncode:
        raise RuntimeError(f"command failed: {args[:4]}\n{result.stdout}\n{result.stderr}")
    return result


def run(base_image):
    project = "httpserver-deployment-" + uuid.uuid4().hex[:10]
    repository = project + "-fixture"
    bad_tag = "a" * 40
    tags = [repository + suffix for suffix in (":old-a", ":old-b", ":" + bad_tag)]
    real_docker = shutil.which("docker")
    if not real_docker:
        raise RuntimeError("Docker CLI is required")
    command([real_docker, "image", "inspect", base_image])
    with tempfile.TemporaryDirectory(prefix=project + "-") as temporary:
        root = Path(temporary)
        shutil.copytree(REPO / "scripts", root / "scripts")
        shutil.copytree(REPO / "db", root / "db")
        topology = yaml.safe_load((REPO / "docker-compose.production.yml").read_text())
        topology.pop("x-web-environment", None)
        topology.pop("x-web-restrictions", None)
        for service in ("webserver-a", "webserver-b"):
            topology["services"][service].pop("build")
        # Only rollout participants are real services; observers stay idle fixtures.
        for service in ("prometheus", "grafana", "alertmanager", "node-exporter"):
            topology["services"][service] = {"image": "alpine:3.22", "command": ["sleep", "600"]}
        (root / "nginx.conf").write_text(
            "upstream backend { server webserver-a:8088; server webserver-b:8088; }\n"
            "server { listen 80; location / { proxy_pass http://backend; } }\n")
        topology["services"]["nginx"] = {
            "image": "nginx:1.27-alpine", "ports": ["127.0.0.1::80"],
            "volumes": [str(root / "nginx.conf") + ":/etc/nginx/conf.d/default.conf:ro"],
        }
        compose_file = root / "docker-compose.production.yml"
        compose_file.write_text(json.dumps(topology))
        env = os.environ.copy()
        for key in list(env):
            if key.startswith(("HTTPSERVER_", "MYSQL_", "COMPOSE_")) or key == "IMAGE_TAG":
                del env[key]
        env.update({
            "COMPOSE_PROJECT_NAME": project, "HTTPSERVER_IMAGE": tags[0],
            "HTTPSERVER_IMAGE_REPOSITORY": repository, "IMAGE_TAG": bad_tag,
            "HTTPSERVER_JWT_SECRET": "isolated-deployment-fixture-secret-over-32-bytes",
            "HTTPSERVER_DB_PASSWORD": uuid.uuid4().hex,
            "MYSQL_ROOT_PASSWORD": uuid.uuid4().hex,
            "HTTPSERVER_PUBLIC_ORIGIN": "https://deployment.example.test",
            "HTTPSERVER_GRAFANA_ADMIN_PASSWORD": uuid.uuid4().hex,
        })
        compose = [real_docker, "compose", "-f", str(compose_file)]
        try:
            for index, tag in enumerate(tags):
                dockerfile = f"FROM {base_image}\nLABEL deployment.fixture={project}-{index}\n"
                if index == 2:
                    dockerfile += "USER root\nRUN printf '#!/bin/sh\\nexit 1\\n' > /app/WebServer\nUSER httpserver\n"
                command([real_docker, "build", "-t", tag, "-"], stdin=dockerfile, timeout=180)
            command(compose + ["up", "-d", "--wait", "mysql", "redis"], env=env, timeout=180)
            for index, service in enumerate(("webserver-a", "webserver-b")):
                command(compose + ["up", "-d", "--no-deps", "--wait", service],
                        env={**env, "HTTPSERVER_IMAGE": tags[index]})
            command(compose + ["up", "-d", "--no-deps", "nginx"], env=env)
            address = command(compose + ["port", "nginx", "80"], env=env).stdout.strip()
            env["HTTPSERVER_SMOKE_URL"] = "http://" + address
            command(["curl", "--fail", "--retry", "5", "--retry-connrefused", "--retry-delay", "1",
                     env["HTTPSERVER_SMOKE_URL"] + "/readyz"])
            previous = {}
            for service in ("webserver-a", "webserver-b"):
                container = command(compose + ["ps", "-q", service], env=env).stdout.strip()
                previous[service] = command([real_docker, "inspect", "--format", "{{.Image}}", container]).stdout.strip()
            (root / "bin").mkdir()
            wrapper = root / "bin/docker"
            wrapper.write_text("#!/usr/bin/env bash\nset -euo pipefail\n"
                               'if [[ "$1" == compose && "$4" == pull ]]; then exit 0; fi\n'
                               'exec "$DEPLOY_REAL_DOCKER" "$@"\n')
            wrapper.chmod(0o755)
            env["DEPLOY_REAL_DOCKER"] = real_docker
            env["PATH"] = str(root / "bin") + ":" + env["PATH"]
            started = time.monotonic()
            result = command(["bash", str(root / "scripts/deploy_release.sh")], env=env,
                             timeout=300, check=False)
            elapsed = time.monotonic() - started
            if result.returncode == 0 or "rollback_acceptance status=passed instances=2" not in result.stderr:
                raise AssertionError(f"rollback did not pass\n{result.stdout}\n{result.stderr}")
            for service in previous:
                container = command(compose + ["ps", "-q", service], env=env).stdout.strip()
                restored = command([real_docker, "inspect", "--format", "{{.Image}}", container]).stdout.strip()
                assert restored == previous[service], (service, restored, previous[service])
                command(compose + ["exec", "-T", service, "curl", "--fail", "--silent",
                                   "http://127.0.0.1:8088/readyz"], env=env)
            command(["curl", "--fail", "--silent", env["HTTPSERVER_SMOKE_URL"] + "/readyz"])
            print(f"deployment_docker_acceptance readiness_failure=detected restored_images=2 "
                  f"proxy_ready=1 migrations=already_applied elapsed_seconds={elapsed:.2f}")
        except Exception:
            diagnostic = command(compose + ["logs", "--no-color", "--tail", "30",
                                             "webserver-a", "webserver-b"], env=env, check=False)
            print(diagnostic.stdout, file=sys.stderr)
            raise
        finally:
            command(compose + ["down", "--volumes", "--remove-orphans"], env=env, check=False)
            for tag in tags:
                command([real_docker, "image", "rm", tag], check=False)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--base-image", default="httpserver:production-candidate")
    run(parser.parse_args().base_image)
