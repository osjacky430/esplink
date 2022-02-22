set(ELF_DIR)
set(ESP_MKBIN_DIR)
set(OUTPUT_DIR)

file(GLOB elf_file_list RELATIVE ${ELF_DIR} "*.elf")

foreach (elf_file IN LISTS elf_file_list)
  get_filename_component(elf_file_name elf_file NAME)
  execute_process(COMMAND esp-mkbin --file ${elf_file} --output ${OUTPUT_DIR}/${elf_file_name} --chip ESP32C3 COMMAND_ERROR_IS_FATAL ANY
                  WORKING_DIRECTORY ${ESP_MKBIN_DIR})
endforeach ()
