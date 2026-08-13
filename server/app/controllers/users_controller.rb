class UsersController < ApplicationController
  before_action :set_user, only: %i[ show ]

  # GET /users
  def index
    @users = User.order(:name)
  end

  # GET /users/:id
  def show
    @user_therapies = @user.user_therapies.includes(:therapy_type, :skin_type).newest_first
    @user_therapy = @user.user_therapies.new
  end


  private
    def set_user
      @user = User.find(params.expect(:id))
    end
end
