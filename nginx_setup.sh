sudo rm /etc/nginx/sites-enabled/default
sudo rm /etc/nginx/conf.d/*
sudo cp ./nginx.conf /etc/nginx/conf.d/alarms.conf
sudo nginx -s reload