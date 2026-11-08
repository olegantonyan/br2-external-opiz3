include $(sort $(wildcard $(BR2_EXTERNAL_OPIZ3_PATH)/package/*/*.mk))

define OPIZ3_CPIO_DROP_BOOT
	rm -rf $(TARGET_DIR)/boot/*
endef
ROOTFS_CPIO_PRE_GEN_HOOKS += OPIZ3_CPIO_DROP_BOOT
