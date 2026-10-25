# GUI designer model + code generator, templates, release packaging (feat-designer)
IDE_AREAS += designer
designer_TESTS = designer_model_test.cpp designer_template_test.cpp designer_release_test.cpp
designer_SRCS  = src/ide/GuiModel.cpp src/ide/TemplateEngine.cpp src/ide/ReleaseGen.cpp
