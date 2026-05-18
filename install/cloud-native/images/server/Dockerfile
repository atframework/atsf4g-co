# 使用官方Debian最新版本作为基础镜像
FROM harbor.m-oa.com:6023/dev-docker/run-images:latest

RUN mkdir -p /data/atframework/publish

ARG SERVER_NAME="lobbysvr"

ARG APP_UID=999
ARG APP_GID=999

# COPY ./publish/ /data/atframework/publish/

COPY ./publish/${SERVER_NAME} /data/atframework/publish/${SERVER_NAME}
COPY ./publish/cloud-native/images/server/entrypoint.sh /entrypoint.sh
COPY ./publish/bin /data/atframework/publish/bin
COPY ./publish/lib /data/atframework/publish/lib
COPY ./publish/resource /data/atframework/publish/resource
COPY ./publish/robot /data/atframework/publish/robot
COPY ./publish/tools /data/atframework/publish/tools

RUN sed -i 's/\r$//' /entrypoint.sh || true && \
	chmod +x /entrypoint.sh

RUN set -eux; \
	if ! getent group tools >/dev/null 2>&1; then \
		groupadd -r tools || addgroup --system tools || true; \
	fi; \
	if ! id -u tools >/dev/null 2>&1; then \
		useradd -r -u ${APP_UID} -g ${APP_GID} -d /home/tools -s /usr/sbin/nologin -M tools || adduser --system --uid ${APP_UID} --ingroup tools --no-create-home --shell /usr/sbin/nologin tools || true; \
	fi; \
	mkdir -p /home/tools /data/atframework/publish/${SERVER_NAME} /opt/atframework/publish/${SERVER_NAME}; \
	chown -R tools:tools /home/tools /data/atframework/publish/${SERVER_NAME} /opt/atframework/publish || true

RUN chown -R tools:tools /data/atframework/publish/${SERVER_NAME}

ENV SERVER_TYPE_NAME=${SERVER_NAME}

WORKDIR /data/atframework/publish/${SERVER_NAME}/bin
USER 999:999

ENTRYPOINT [ "/bin/bash", "/entrypoint.sh" ]