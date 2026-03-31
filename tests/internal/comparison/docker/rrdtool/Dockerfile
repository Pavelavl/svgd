FROM alpine:3.19

RUN apk add --no-cache rrdtool fcgiwrap spawn-fcgi nginx bash

RUN mkdir -p /usr/lib/cgi-bin /var/log/nginx /run/nginx /var/run

COPY cgi/graph.cgi /usr/lib/cgi-bin/
COPY nginx.conf /etc/nginx/http.d/default.conf

RUN sed -i 's/\r$//' /usr/lib/cgi-bin/graph.cgi && \
    chmod +x /usr/lib/cgi-bin/graph.cgi

VOLUME /var/lib/collectd/rrd

EXPOSE 8080

# Create nginx user and set proper permissions
RUN adduser -D -g 'www-data' www-data 2>/dev/null || true

CMD ["sh", "-c", "spawn-fcgi -s /var/run/fcgi.sock -M 0666 /usr/bin/fcgiwrap && nginx -g 'daemon off;'"]
