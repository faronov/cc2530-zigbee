# SPDX-License-Identifier: BSD-3-Clause
"""Shared fail-closed acceptance predicate for the sustained router workload."""


def service_valid(service):
    return (
        type(service) is dict
        and set(service) == {
            "duration_ms", "protected_frames", "replay_drops", "reports",
            "erases", "programs", "ordinary_rx_commands", "security_checkpoints",
            "flash_controller_nominal_us"
        }
        and all(type(v) is int and v >= 0 for v in service.values())
        and 600000 <= service["duration_ms"] < 601000
        and service["protected_frames"] == 120
        and service["replay_drops"] == 12
        and service["reports"] >= 16
        and service["erases"] <= 8
        and service["programs"] == 45 * service["erases"]
        and service["ordinary_rx_commands"] == 0
        and service["security_checkpoints"] <= service["erases"]
        and service["flash_controller_nominal_us"] ==
            20000 * service["erases"] + 20 * service["programs"]
    )
