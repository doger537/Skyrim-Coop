-- Pure bounded codecs are also used by the client. Do not link the SQLite
-- campaign service into the game process just to decode a receipt.
target("TrueCoopData")
    set_kind("static")
    set_group("common")
    add_includedirs(".", {public = true})
    add_files("CampaignData.cpp")
    add_deps("SkyrimEncoding")
    add_packages("tiltedcore", "hopscotch-map", "glm", {public = true})

target("TrueCoopState")
    set_kind("static")
    set_group("common")
    add_includedirs(".", {public = true})
    add_headerfiles("**.h")
    add_files("**.cpp|CampaignData.cpp")
    add_deps("TrueCoopData")
    add_packages("tiltedcore", "hopscotch-map", "glm", "cryptopp", "sqlite3", {public = true})
    add_packages("nlohmann_json")
