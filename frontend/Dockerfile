# 多阶段构建：构建阶段
FROM node:18-alpine as build-stage

WORKDIR /app

# 复制包定义文件
COPY package*.json ./

# 安装依赖
RUN npm ci

# 复制源代码
COPY . .

# 构建应用
RUN npm run build

# 运行阶段：使用 Nginx 提供静态文件服务
FROM nginx:alpine as production-stage

# 复制构建产物到 Nginx 目录
COPY --from=build-stage /app/dist /usr/share/nginx/html

# 暴露端口
EXPOSE 3000

CMD ["nginx", "-g", "daemon off;"]
