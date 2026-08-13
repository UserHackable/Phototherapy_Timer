Rails.application.routes.draw do
  resource :session
  resources :passwords, param: :token
  resources :devices do
    post :ota_check, on: :member
  end

  resources :users, only: %i[index show] do
    resources :exposures
  end

  # Reveal health status on /up that returns 200 if the app boots with no exceptions, otherwise 500.
  # Can be used by load balancers and uptime monitors to verify that the app is live.
  get "up" => "rails/health#show", as: :rails_health_check

  # ESP LAN OTA — unauthenticated; files under storage/firmware/<app>/
  get "firmware/:app/manifest.json", to: "firmware#manifest", constraints: { app: /[A-Za-z0-9_-]+/ }
  get "firmware/:app/app.bin", to: "firmware#app_bin", constraints: { app: /[A-Za-z0-9_-]+/ }

  # Render dynamic PWA files from app/views/pwa/* (remember to link manifest in application.html.erb)
  # get "manifest" => "rails/pwa#manifest", as: :pwa_manifest
  # get "service-worker" => "rails/pwa#service_worker", as: :pwa_service_worker

  root "devices#index"
end

