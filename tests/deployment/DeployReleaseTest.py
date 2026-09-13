#!/usr/bin/env python3
"""Exercise the real release scripts with isolated command/process fixtures."""
import fcntl
import gzip
import json
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


REPO = Path(__file__).resolve().parents[2]
SHA = "a" * 40
OLD_IMAGES = {"webserver-a": "sha256:" + "b" * 64, "webserver-b": "sha256:" + "c" * 64}


class DeployReleaseTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="httpserver-deploy-test-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        shutil.copytree(REPO / "scripts", self.root / "scripts")
        shutil.copytree(REPO / "db", self.root / "db")
        shutil.copyfile(REPO / "docker-compose.production.yml", self.root / "docker-compose.production.yml")
        (self.root / "bin").mkdir()
        fixture = self.root / "bin" / "fixture"
        shutil.copyfile(REPO / "tests/deployment/FakeDeployCommand.py", fixture)
        fixture.chmod(0o755)
        for name in ("docker", "curl", "sleep", "mysql"):
            (self.root / "bin" / name).symlink_to(fixture)
        self.state_path = self.root / "state.json"
        self.state_path.write_text(json.dumps({
            "scenario": "success", "images": OLD_IMAGES.copy(),
            "versions": ["001_initial", "002_outbox_leases"],
            "calls": [], "migrations": [], "failed": [], "stopped": [],
        }))
        self.env = {key: value for key, value in os.environ.items()
                    if not key.startswith(("HTTPSERVER_", "IMAGE_TAG", "MYSQL_"))}
        self.env.update({
            "PATH": f"{self.root / 'bin'}:{os.environ['PATH']}",
            "DEPLOY_FIXTURE_STATE": str(self.state_path), "IMAGE_TAG": SHA,
            "HTTPSERVER_IMAGE_REPOSITORY": "fixture.local/httpserver",
            "MYSQL_ROOT_PASSWORD": "fixture-only-root-password",
        })

    def state(self):
        return json.loads(self.state_path.read_text())

    def configure(self, **updates):
        state = self.state()
        state.update(updates)
        self.state_path.write_text(json.dumps(state))

    def deploy(self):
        return subprocess.run(["bash", str(self.root / "scripts/deploy_release.sh")],
                              env=self.env, text=True, capture_output=True, timeout=30)

    def assert_restored(self, result):
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertEqual(self.state()["images"], OLD_IMAGES)
        self.assertIn("rollback_acceptance status=passed instances=2", result.stderr)
        self.assertNotIn("deployment_acceptance", result.stdout)
        calls = self.state()["calls"]
        for service in OLD_IMAGES:
            self.assertTrue(any(service in call["args"] and "curl" in call["args"] and
                                call["images"].get(service) == OLD_IMAGES[service]
                                for call in calls))
        proxy_calls = [call for call in calls if "nginx" in call["args"]]
        self.assertEqual(proxy_calls[-1]["args"][-3:], ["nginx", "-s", "reload"])

    def test_success_skips_applied_migrations_and_secures_backup(self):
        result = self.deploy()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("deployment_acceptance", result.stdout)
        self.assertEqual(self.state()["migrations"], [])
        self.assertEqual(set(self.state()["images"].values()), {"fixture.local/httpserver:" + SHA})
        backups = list((self.root / "backups").glob("*.sql.gz"))
        self.assertEqual(len(backups), 1)
        self.assertEqual(backups[0].stat().st_mode & 0o777, 0o600)
        self.assertIn("fixture SQL dump", gzip.decompress(backups[0].read_bytes()).decode())
        self.assertEqual(list((self.root / "backups").glob("*.partial.*")), [])
        self.assertEqual(self.state()["calls"][-1]["args"][-3:], ["nginx", "-s", "reload"])

    def test_new_database_and_repeat_deployment_apply_migrations_once(self):
        self.configure(versions=[])
        for _ in range(2):
            result = self.deploy()
            self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.state()["migrations"], ["001_initial", "002_outbox_leases"])

    def test_standalone_migration_script_uses_same_ledger(self):
        self.configure(versions=[])
        self.env.update({"HTTPSERVER_DB_HOST": "fixture", "HTTPSERVER_DB_USER": "fixture",
                         "HTTPSERVER_DB_PASSWORD": "fixture", "HTTPSERVER_DB_NAME": "webserver"})
        for _ in range(2):
            result = subprocess.run(["bash", str(self.root / "scripts/apply_migrations.sh")],
                                    env=self.env, text=True, capture_output=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.state()["migrations"], ["001_initial", "002_outbox_leases"])

    def test_each_instance_readiness_failure_restores_both_original_images(self):
        for service in OLD_IMAGES:
            with self.subTest(service=service):
                self.configure(scenario=service, images=OLD_IMAGES.copy(), calls=[], failed=[])
                self.assert_restored(self.deploy())

    def test_partial_activation_and_nginx_failures_restore_previous_release(self):
        for scenario in ("activation", "nginx_config", "nginx_reload"):
            with self.subTest(scenario=scenario):
                self.configure(scenario=scenario, images=OLD_IMAGES.copy(), calls=[], failed=[])
                self.assert_restored(self.deploy())

    def test_public_readiness_failure_restores_previous_release(self):
        self.configure(scenario="smoke")
        self.env["HTTPSERVER_SMOKE_URL"] = "https://fixture.local"
        self.assert_restored(self.deploy())
        public_probe = next(call for call in self.state()["calls"] if call["command"] == "curl")
        self.assertEqual(public_probe["args"][-1], "https://fixture.local/readyz")

    def test_pre_activation_failures_do_not_restart_healthy_applications(self):
        for scenario in ("pull", "dependency", "backup", "migration"):
            with self.subTest(scenario=scenario):
                self.configure(scenario=scenario, images=OLD_IMAGES.copy(), calls=[], versions=[])
                result = self.deploy()
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(self.state()["images"], OLD_IMAGES)
                self.assertNotIn("rollback_acceptance", result.stderr)
                activations = [call for call in self.state()["calls"]
                               if "up" in call["args"] and "webserver-a" in call["args"]]
                self.assertEqual(activations, [])
                self.assertEqual(list((self.root / "backups").glob("*.partial.*")), [])

    def test_failed_rollback_is_reported_as_failure(self):
        self.configure(scenario="rollback")
        result = self.deploy()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("rollback_acceptance status=failed", result.stderr)
        self.assertNotIn("status=passed", result.stderr)

    def test_first_deployment_failure_stops_failed_applications(self):
        self.configure(scenario="webserver-a", images={})
        result = self.deploy()
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.state()["stopped"], ["webserver-a", "webserver-b"])
        self.assertIn("status=unavailable restored_instances=0", result.stderr)

    def test_signal_during_activation_rolls_back(self):
        self.configure(scenario="termination")
        result = self.deploy()
        self.assert_restored(result)
        self.assertEqual(result.returncode, 143)

    def test_explicit_release_tag_wins_over_env_defaults(self):
        (self.root / ".env").write_text("IMAGE_TAG=" + "d" * 40 + "\n")
        result = self.deploy()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(set(self.state()["images"].values()), {"fixture.local/httpserver:" + SHA})

    def test_concurrent_deploy_is_rejected_before_docker_mutation(self):
        (self.root / "backups").mkdir()
        with (self.root / "backups/.deployment.lock").open("w") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            result = self.deploy()
        self.assertEqual(result.returncode, 75)
        self.assertEqual(self.state()["calls"], [])

    def test_mutable_tag_is_rejected(self):
        self.env["IMAGE_TAG"] = "latest"
        result = self.deploy()
        self.assertEqual(result.returncode, 2)
        self.assertEqual(self.state()["calls"], [])


if __name__ == "__main__":
    unittest.main(verbosity=2)
