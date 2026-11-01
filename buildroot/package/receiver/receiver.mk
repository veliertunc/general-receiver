RECEIVER_VERSION = 1.0
RECEIVER_SITE = $(BR2_EXTERNAL_RECEIVER_PATH)/..
RECEIVER_SITE_METHOD = local

define RECEIVER_INSTALL_CONFIG
	$(INSTALL) -D -m 0644 $(@D)/config/receiver.conf $(TARGET_DIR)/etc/receiver/receiver.conf
	$(INSTALL) -D -m 0644 $(@D)/config/channels.conf $(TARGET_DIR)/etc/receiver/channels.conf
	$(INSTALL) -D -m 0755 $(@D)/buildroot/board/S50receiver $(TARGET_DIR)/etc/init.d/S50receiver
endef
RECEIVER_POST_INSTALL_TARGET_HOOKS += RECEIVER_INSTALL_CONFIG
$(eval $(cmake-package))
