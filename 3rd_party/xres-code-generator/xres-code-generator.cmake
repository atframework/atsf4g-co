# =========== 3rdparty xres-code-generator ==================
set(3RD_PARTY_XRESCODE_GENERATOR_VERSION "master")
set(3RD_PARTY_XRESCODE_GENERATOR_REPO_DIR
    "${PROJECT_3RD_PARTY_PACKAGE_DIR}/xres-code-generator-${3RD_PARTY_XRESCODE_GENERATOR_VERSION}")
set(3RD_PARTY_XRESCODE_GENERATOR_PY "${3RD_PARTY_XRESCODE_GENERATOR_REPO_DIR}/xrescode-gen.py")

if(NOT EXISTS ${3RD_PARTY_XRESCODE_GENERATOR_PY})
  project_git_clone_3rd_party(
    URL
    "https://github.com/xresloader/xres-code-generator.git"
    REPO_DIRECTORY
    ${3RD_PARTY_XRESCODE_GENERATOR_REPO_DIR}
    DEPTH
    200
    BRANCH
    ${3RD_PARTY_XRESCODE_GENERATOR_VERSION}
    WORKING_DIRECTORY
    ${PROJECT_3RD_PARTY_PACKAGE_DIR}
    CHECK_PATH
    "xrescode-gen.py")
endif()
