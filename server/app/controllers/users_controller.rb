class UsersController < ApplicationController
  before_action :set_user, only: %i[ show ]

  # GET /users
  def index
    @users = User.order(:name)
  end

  # GET /users/:id
  def show
  end

  private
    def set_user
      @user = User.find(params.expect(:id))
    end
end
