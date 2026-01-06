# 使用官方Debian最新版本作为基础镜像
FROM harbor.m-oa.com:6023/dev-docker/run-images:latest

RUN mkdir -p /data/projecty

ARG SERVER_NAME="lobbysvr"

COPY . /data/projecty
COPY ./deploy/images/server/entrypoint.sh /entrypoint.sh
RUN sed -i 's/\r$//' /entrypoint.sh || true && \
	chmod +x /entrypoint.sh

RUN set -eux; \
	if ! getent group tools >/dev/null 2>&1; then \
		groupadd -r tools || addgroup --system tools || true; \
	fi; \
	if ! id -u tools >/dev/null 2>&1; then \
		useradd -r -g tools -d /home/tools -s /usr/sbin/nologin -M tools || adduser --system --ingroup tools --no-create-home --shell /usr/sbin/nologin tools || true; \
	fi; \
	mkdir -p /home/tools /data/projecty; \
	chown -R tools:tools /home/tools /data/projecty || true

RUN chown -R tools:tools /data/projecty 

ENV SERVER_TYPE_NAME=${SERVER_NAME}

WORKDIR /data/projecty/${SERVER_NAME}/bin
USER tools

ENTRYPOINT [ "/bin/bash", "/entrypoint.sh" ]