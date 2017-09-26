include $(TOPDIR)/rules.mk

PKG_NAME:=qca9531_test
PKG_RELEASE:=1

PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)

include $(INCLUDE_DIR)/package.mk


define Package/qca9531_test
	SECTION:=utils
	CATEGORY:=Utilities
	TITLE:=QCA9531 Test
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) ./src/* $(PKG_BUILD_DIR)/
endef

define Package/qca9531_test/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/qca9531_test $(1)/usr/bin/
endef

$(eval $(call BuildPackage,qca9531_test))
