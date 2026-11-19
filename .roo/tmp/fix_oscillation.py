import sys

path = "/home/astro/development/astro_mount_control/src/controllers/mount_controller.cpp"
with open(path, "r") as f:
    s = f.read()

SP24 = " " * 24
SP28 = " " * 28
SP32 = " " * 32
SP36 = " " * 36
SP40 = " " * 40
SP41 = " " * 41
SP45 = " " * 45

# 1. Equatorial tracking-loop catch-up
old1 = (
    SP32 + "if (std::abs(new_axis1_target - ref1) > close1) {\n"
    + SP36 + "pos_vel_axis1 = config_.mount_config.max_slew_rate;\n"
    + SP32 + "}\n"
    + SP32 + "if (std::abs(new_axis2_target - ref2) > close2) {\n"
    + SP36 + "pos_vel_axis2 = config_.mount_config.max_slew_rate;\n"
    + SP32 + "}"
)
new1 = (
    SP32 + "if (std::abs(new_axis1_target - ref1) > close1) {\n"
    + SP36 + "pos_vel_axis1 = std::max(pos_vel_axis1,\n"
    + SP40 + "std::min(config_.mount_config.max_slew_rate,\n"
    + SP45 + "std::abs(new_axis1_target - ref1)));\n"
    + SP32 + "}\n"
    + SP32 + "if (std::abs(new_axis2_target - ref2) > close2) {\n"
    + SP36 + "pos_vel_axis2 = std::max(pos_vel_axis2,\n"
    + SP40 + "std::min(config_.mount_config.max_slew_rate,\n"
    + SP45 + "std::abs(new_axis2_target - ref2)));\n"
    + SP32 + "}"
)

# 2. ALT_AZ / CASUAL tracking-loop catch-up
old2 = (
    SP32 + "if (std::abs(new_axis1_target - ref1) > close1) pos_vel_axis1 = config_.mount_config.max_slew_rate;\n"
    + SP32 + "if (std::abs(new_axis2_target - ref2) > close2) pos_vel_axis2 = config_.mount_config.max_slew_rate;"
)
new2 = (
    SP32 + "if (std::abs(new_axis1_target - ref1) > close1) {\n"
    + SP36 + "pos_vel_axis1 = std::max(pos_vel_axis1,\n"
    + SP40 + "std::min(config_.mount_config.max_slew_rate,\n"
    + SP45 + "std::abs(new_axis1_target - ref1)));\n"
    + SP32 + "}\n"
    + SP32 + "if (std::abs(new_axis2_target - ref2) > close2) {\n"
    + SP36 + "pos_vel_axis2 = std::max(pos_vel_axis2,\n"
    + SP40 + "std::min(config_.mount_config.max_slew_rate,\n"
    + SP45 + "std::abs(new_axis2_target - ref2)));\n"
    + SP32 + "}"
)

# 3. Post-slew verification nudge
old3 = (
    SP24 + "const double vel = config_.mount_config.max_slew_rate;\n"
    + SP24 + "const double acc = config_.mount_config.slew_acceleration;\n"
    + SP24 + "try {\n"
    + SP28 + "if (!axis1_ok) hal_axis1_motor_->setPosition(axis1_target_, vel, acc);\n"
    + SP28 + "if (!axis2_ok) hal_axis2_motor_->setPosition(axis2_target_, vel, acc);"
)
new3 = (
    SP24 + "const double acc = config_.mount_config.slew_acceleration;\n"
    + SP24 + "try {\n"
    + SP28 + "if (!axis1_ok) {\n"
    + SP32 + "const double vel1 = std::max(1.0,\n"
    + SP36 + "std::min(config_.mount_config.max_slew_rate,\n"
    + SP41 + "std::abs(pos0 - axis1_target_) * 2.0));\n"
    + SP32 + "hal_axis1_motor_->setPosition(axis1_target_, vel1, acc);\n"
    + SP28 + "}\n"
    + SP28 + "if (!axis2_ok) {\n"
    + SP32 + "const double vel2 = std::max(1.0,\n"
    + SP36 + "std::min(config_.mount_config.max_slew_rate,\n"
    + SP41 + "std::abs(pos1 - axis2_target_) * 2.0));\n"
    + SP32 + "hal_axis2_motor_->setPosition(axis2_target_, vel2, acc);\n"
    + SP28 + "}"
)

replaced = 0
for name, old, new in [("equatorial", old1, new1), ("altaz", old2, new2), ("verify", old3, new3)]:
    n = s.count(old)
    if n != 1:
        print("ERROR: pattern '%s' found %d times (expected 1)" % (name, n))
        sys.exit(1)
    s = s.replace(old, new)
    replaced += 1
    print("applied:", name)

with open(path, "w") as f:
    f.write(s)

print("OK: %d replacements written" % replaced)
