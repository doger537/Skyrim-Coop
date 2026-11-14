-- Unlike xmake's built-in GIT_TAG, this also identifies clones without release tags.
function main()
    import("detect.tools.find_git")
    local git = assert(find_git(), "Git is required to identify the True Coop build")
    local branch = os.iorunv(git, {"rev-parse", "--abbrev-ref", "HEAD"}):trim()
    local revision = os.iorunv(git, {"describe", "--tags", "--always"}):trim()
    assert(revision ~= "" and revision ~= "none", "Cannot determine the True Coop source revision")
    return branch, revision
end
