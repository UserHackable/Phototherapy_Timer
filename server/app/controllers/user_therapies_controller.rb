class UserTherapiesController < ApplicationController
  before_action :set_user

  # POST /users/:user_id/user_therapies
  def create
    @user_therapy = @user.user_therapies.new(user_therapy_params)

    if @user_therapy.save
      redirect_to @user, notice: "Therapy was assigned."
    else
      prepare_user_show
      render "users/show", status: :unprocessable_content
    end
  end

  # DELETE /users/:user_id/user_therapies/:id
  def destroy
    @user.user_therapies.find(params.expect(:id)).destroy!
    redirect_to @user, notice: "Therapy was removed.", status: :see_other
  end

  private

  def set_user
    @user = User.find(params.expect(:user_id))
  end

  def prepare_user_show
    @user_therapies = @user.user_therapies.includes(:therapy_type, :skin_type).newest_first
  end

  def user_therapy_params
    permitted = params.expect(user_therapy: [ :therapy_type_id, :skin_type_id ])
    permitted[:skin_type_id] = nil if permitted[:skin_type_id].blank?
    permitted
  end
end
