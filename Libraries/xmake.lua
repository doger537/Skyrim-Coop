if is_plat("windows") then
	includes("./TiltedUI")
	includes("./TiltedReverse")
	includes("./TiltedHooks")
end

includes("./TiltedConnect")

-- Keep the pinned submodule intact. This overlay fixes its process-global
-- callback pump when several clients share one process (the protocol probe).
target("TiltedConnect")
    remove_files("TiltedConnect/Code/connect/src/Client.cpp")
    add_files("../Code/transport/Client.cpp")
target_end()
